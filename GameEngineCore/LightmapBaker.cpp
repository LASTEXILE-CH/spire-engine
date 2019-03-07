#include "LightmapBaker.h"
#include "ObjectSpaceGBufferRenderer.h"
#include "StaticSceneRenderer.h"
#include "ObjectSpaceMapSet.h"
#include "Level.h"
#include "StaticMeshActor.h"
#include "VectorMath.h"
#include "Engine.h"
#include "CameraActor.h"
#include "CoreLib/Threading.h"
#include <atomic>

namespace GameEngine
{
    using namespace CoreLib;
    static int pixelCounter = 0;
    static bool threadCancelled = false;
    static unsigned int threadRandomSeed = 0;
    #pragma omp threadprivate(pixelCounter)
    #pragma omp threadprivate(threadCancelled)
    #pragma omp threadprivate(threadRandomSeed)

    class LightmapBakerImpl : public LightmapBaker
    {
    public:
        LightmapBakingSettings settings;
        LightmapSet lightmaps;
    private:
        struct RawMapSet
        {
            RawObjectSpaceMap lightMap, indirectLightmap, diffuseMap, normalMap, positionMap;
            IntSet validPixels;
            void Init(int w, int h)
            {
                lightMap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
                indirectLightmap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
                diffuseMap.Init(RawObjectSpaceMap::DataType::RGBA8, w, h);
                normalMap.Init(RawObjectSpaceMap::DataType::RGB10_X2_SIGNED, w, h);
                positionMap.Init(RawObjectSpaceMap::DataType::RGBA32F, w, h);
                validPixels.SetMax(w * h);
            }
        };
        List<RawMapSet> maps;
        Level* level = nullptr;
        RefPtr<StaticScene> staticScene;
        void AllocLightmaps()
        {
            // in the future we may support a wider range of actors.
            // for now we only allocate lightmaps for static mesh actors.
            List<int> mapResolutions;
            for (auto actor : level->Actors)
            {
                if (auto smActor = actor.Value.As<StaticMeshActor>())
                {
                    auto size = (smActor->Bounds.Max - smActor->Bounds.Min).Length();
                    int resolution = Math::Clamp(1 << Math::Log2Ceil((int)(size * settings.ResolutionScale)), settings.MinResolution, settings.MaxResolution);
                    lightmaps.ActorLightmapIds[actor.Value.Ptr()] = mapResolutions.Count();
                    mapResolutions.Add(resolution);
                }
            }
            maps.SetSize(mapResolutions.Count());
            for (int i = 0; i < maps.Count(); i++)
                maps[i].Init(mapResolutions[i], mapResolutions[i]);
            lightmaps.Lightmaps.SetSize(mapResolutions.Count());
        }
        template<typename TResult, typename TSource, typename TSelectFunc>
        void ReadAndDownSample(Texture2D* texture, TResult* dstBuffer, TSource srcZero, int w, int h, int superSample, const TSelectFunc & select)
        {
            auto dstZero = select(srcZero);
            List<TSource> buffer;
            buffer.SetSize(w * h);
            texture->GetData(0, buffer.Buffer(), w * h * sizeof(TSource));
            for (int y = 0; y < h / superSample; y++)
                for (int x = 0; x < w / superSample; x++)
                {
                    int destId = y * (h / superSample) + x;
                    dstBuffer[destId] = dstZero;
                    for (int xx = 0; xx < superSample; xx++)
                    {
                        for (int yy = 0; yy < superSample; yy++)
                        {
                            auto src = buffer[(y + yy) * w + x + xx];
                            if (src != srcZero)
                            {
                                dstBuffer[destId] = select(src);
                                goto procEnd;
                            }
                        }
                    }
                procEnd:;
                }

        }
        void BakeLightmapGBuffers()
        {
            const int SuperSampleFactor = 1;
            HardwareRenderer* hwRenderer = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            RefPtr<ObjectSpaceGBufferRenderer> renderer = CreateObjectSpaceGBufferRenderer();
            renderer->Init(Engine::Instance()->GetRenderer()->GetHardwareRenderer(), Engine::Instance()->GetRenderer()->GetRendererService(), "LightmapGBufferGen.slang");
            int progress = 0;
            StatusChanged("Baking G-Buffers...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            for (auto actor : level->Actors)
            {
                auto mapId = lightmaps.ActorLightmapIds.TryGetValue(actor.Value.Ptr());
                if (!mapId) continue;
                auto map = maps.Buffer() + *mapId;
                int width = map->diffuseMap.Width * SuperSampleFactor;
                int height = map->diffuseMap.Height * SuperSampleFactor;
                RefPtr<Texture2D> texDiffuse = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_8);
                RefPtr<Texture2D> texPosition = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texNormal = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texDepth = hwRenderer->CreateTexture2D(TextureUsage::SampledDepthAttachment, width, height, 1, StorageFormat::Depth32);
                Array<Texture2D*, 4> dest;
                Array<StorageFormat, 4> formats;
                dest.SetSize(4);
                dest[0] = texDiffuse.Ptr();
                dest[1] = texPosition.Ptr();
                dest[2] = texNormal.Ptr();
                dest[3] = texDepth.Ptr();
                formats.SetSize(4);
                formats[0] = StorageFormat::RGBA_8;
                formats[1] = StorageFormat::RGBA_F32;
                formats[2] = StorageFormat::RGBA_F32;
                formats[3] = StorageFormat::Depth32;
                renderer->RenderObjectSpaceMap(dest.GetArrayView(), formats.GetArrayView(), actor.Value.Ptr(), map->diffuseMap.Width, map->diffuseMap.Height);

                ReadAndDownSample(texDiffuse.Ptr(), (uint32_t*)map->diffuseMap.GetBuffer(), (uint32_t)0, width, height, SuperSampleFactor, 
                    [](uint32_t x) {return x; });
                ReadAndDownSample(texPosition.Ptr(), (VectorMath::Vec4*)map->positionMap.GetBuffer(), VectorMath::Vec4::Create(0.0f), width, height, SuperSampleFactor, 
                    [](VectorMath::Vec4 x) {return x; });
                ReadAndDownSample(texNormal.Ptr(), (uint32_t*)map->normalMap.GetBuffer(), VectorMath::Vec4::Create(0.0f), width, height, SuperSampleFactor,
                    [](VectorMath::Vec4 x) {return PackRGB10(x.x, x.y, x.z); });
                for (int i = 0; i < height; i++)
                    for (int j = 0; j < width; j++)
                    {
                        auto diffusePixel = map->diffuseMap.GetPixel(j, i);
                        if (diffusePixel.x > 1e-5f || diffusePixel.y > 1e-5f || diffusePixel.z > 1e-5f)
                            map->validPixels.Add(i * width + j);
                    }
                progress++;
                ProgressChanged(LightmapBakerProgressChangedEventArgs(progress, lightmaps.ActorLightmapIds.Count()));
            }
        }

        void RenderDebugView(String fileName)
        {
            List<RawObjectSpaceMap*> diffuseMaps;
            diffuseMaps.SetSize(maps.Count());
            for (int i = 0; i < diffuseMaps.Count(); i++)
            {
                diffuseMaps[i] = &maps[i].diffuseMap;
            }
            RefPtr<StaticSceneRenderer> staticRenderer = CreateStaticSceneRenderer();
            staticRenderer->SetCamera(level->CurrentCamera->GetCameraTransform(), level->CurrentCamera->FOV, 1024, 1024);
            auto & image = staticRenderer->Render(staticScene.Ptr(), diffuseMaps, lightmaps);
            image.GetImageRef().SaveAsBmpFile(fileName);
        }

        float Lerp(float a, float b, float t)
        {
            return a * (1.0f - t) + b * t;
        }

        VectorMath::Vec3 TraceShadowRay(Ray & shadowRay)
        {
            auto inter = staticScene->TraceRay(shadowRay);
            if (inter.IsHit)
                return VectorMath::Vec3::Create(0.0f, 0.0f, 0.0f);

            return VectorMath::Vec3::Create(1.0f, 1.0f, 1.0f);
        }

        VectorMath::Vec3 ComputeDirectLighting(VectorMath::Vec3 pos, VectorMath::Vec3 normal)
        {
            VectorMath::Vec3 result;
            result.SetZero();
            for (auto & light : staticScene->lights)
            {
                switch (light.Type)
                {
                case StaticLightType::Directional:
                {
                    auto l = light.Direction;
                    auto nDotL = VectorMath::Vec3::Dot(normal, l);
                    Ray shadowRay;
                    shadowRay.tMax = FLT_MAX;
                    shadowRay.Origin = pos;
                    shadowRay.Dir = l;
                    auto p1 = shadowRay.Origin + shadowRay.Dir * 1000.0f;
                    auto shadowFactor = TraceShadowRay(shadowRay);
                    result += light.Intensity * shadowFactor * Math::Max(0.0f, nDotL);
                    break;
                }
                case StaticLightType::Point:
                case StaticLightType::Spot:
                {
                    auto l = light.Position - pos;
                    auto dist = l.Length();
                    if (dist < light.Radius)
                    {
                        auto invDist = 1.0f / dist;
                        l *= invDist;
                        auto nDotL = VectorMath::Vec3::Dot(normal, l);
                        float actualDecay = 1.0f / Math::Max(1.0f, dist * light.Decay);
                        if (light.Type == StaticLightType::Spot)
                        {
                            float ang = acos(VectorMath::Vec3::Dot(l, light.Direction));
                            actualDecay *= Lerp(1.0, 0.0, Math::Clamp((ang - light.SpotFadingStartAngle) / (light.SpotFadingEndAngle - light.SpotFadingStartAngle), 0.0f, 1.0f));
                        }
                        Ray shadowRay;
                        shadowRay.tMax = dist;
                        shadowRay.Origin = pos;
                        shadowRay.Dir = l;
                        auto shadowFactor = TraceShadowRay(shadowRay);
                        result += light.Intensity * actualDecay * shadowFactor * Math::Max(0.0f, nDotL);
                    }
                    break;
                }
                }
            }
            return result;
        }

        VectorMath::Vec3 UniformSampleHemisphere(float r1, float r2)
        {
            float sinTheta = sqrtf(1 - r1 * r1);
            float phi = 2.0f * Math::Pi * r2;
            float x = sinTheta * cosf(phi);
            float z = sinTheta * sinf(phi);
            return VectorMath::Vec3::Create(x, r1, z);
        }

        VectorMath::Vec3 ComputeIndirectLighting(Random & random, VectorMath::Vec3 pos, VectorMath::Vec3 normal, int sampleCount)
        {
            VectorMath::Vec3 result;
            result.SetZero();
            static const float invPi = 1.0f / Math::Pi;
            VectorMath::Vec3 tangent;
            VectorMath::GetOrthoVec(tangent, normal);
            auto binormal = VectorMath::Vec3::Cross(tangent, normal);
            for (int i = 0; i < sampleCount; i++)
            {
                float r1 = random.NextFloat();
                float r2 = random.NextFloat();
                Ray ray;
                ray.Origin = pos;
                auto tanDir = UniformSampleHemisphere(r1, r2);
                ray.Dir = tangent * tanDir.x + normal * tanDir.y + binormal * tanDir.z;
                ray.tMax = FLT_MAX;
                auto inter = staticScene->TraceRay(ray);
                if (inter.IsHit)
                {
                    if (VectorMath::Vec3::Dot(inter.Normal, ray.Dir) < 0.0f)
                    {
                        auto directDiffuse = maps[inter.MapId].lightMap.Sample(inter.UV).xyz();
                        auto indirectDiffuse = maps[inter.MapId].indirectLightmap.Sample(inter.UV).xyz();
                        auto color = directDiffuse + indirectDiffuse;
                        color *= maps[inter.MapId].diffuseMap.Sample(inter.UV).xyz();
                        color *= r1;
                        result += color;
                    }
                }
                else
                {
                    result += staticScene->ambientColor * r1;
                }
            }
            result *= 2.0f / (float)sampleCount;
            return result;
        }

        void BiasGBufferPositions()
        {
            VectorMath::Vec3 tangentDirs[] =
            {
                VectorMath::Vec3::Create(1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(-1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, 1.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, -1.0f)
            };
            for (auto & map : maps)
            {
                int imageSize = map.diffuseMap.Width * map.diffuseMap.Height;
                if (isCancelled) return;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    int x = pixelIdx % map.diffuseMap.Width;
                    int y = pixelIdx / map.diffuseMap.Width;
                    auto diffuse = map.diffuseMap.GetPixel(x, y);
                    // compute lighting only for lightmap pixels that represent valid surface regions
                    if (diffuse.x > 1e-6f || diffuse.y > 1e-6f || diffuse.z > 1e-6f)
                    {
                        auto posPixel = map.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto bias = posPixel.w * 0.6f;
                        auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                        // shoot random rays and find if the ray hits the back of some nearby face,
                        // if so, shift pos to the front of that face to avoid shadow leaking
                        VectorMath::Vec3 tangent, binormal;
                        VectorMath::GetOrthoVec(tangent, normal);
                        binormal = VectorMath::Vec3::Cross(normal, tangent);
                        auto biasedPos = pos + normal * settings.ShadowBias;
                        float minT = FLT_MAX;
                        for (auto tangentDir : tangentDirs)
                        {
                            auto testDir = tangent * tangentDir.x + normal * tangentDir.y + binormal * tangentDir.z;
                            Ray testRay;
                            testRay.Origin = biasedPos;
                            testRay.Dir = testDir;
                            testRay.tMax = bias;
                            auto inter = staticScene->TraceRay(testRay);
                            if (inter.IsHit && VectorMath::Vec3::Dot(inter.Normal, testDir) > 0.0f)
                            {
                                if (inter.T < minT)
                                {
                                    minT = inter.T;
                                    biasedPos = testRay.Origin + testRay.Dir * inter.T + inter.Normal * settings.ShadowBias;
                                }
                            }
                        }
                        map.positionMap.SetPixel(x, y, VectorMath::Vec4::Create(biasedPos, posPixel.w));
                    }
                }
            }
        }

        void ComputeLightmaps_Direct()
        {
            static int iteration = 0;
            iteration++;
            for (auto & map : maps)
            {
                if (isCancelled) return;
                int imageSize = map.diffuseMap.Width * map.diffuseMap.Height;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    if (map.validPixels.Contains(pixelIdx))
                    {
                        pixelCounter++;
                        if (threadCancelled || (pixelCounter & 15) == 0)
                        {
                            threadCancelled = isCancelled;
                        }
                        if (threadCancelled) continue;
                        int x = pixelIdx % map.diffuseMap.Width;
                        int y = pixelIdx / map.diffuseMap.Width;
                        
                        VectorMath::Vec4 lighting;
                        lighting.SetZero();
                        auto diffuse = map.diffuseMap.GetPixel(x, y);
                        auto posPixel = map.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                        lighting = VectorMath::Vec4::Create(ComputeDirectLighting(pos, normal), 1.0f);
                        map.lightMap.SetPixel(x, y, lighting);
                    }
                }
            }
        }

        void ComputeLightmaps_Indirect(int sampleCount)
        {
            static int iteration = 0;
            iteration++;
            for (auto & map : maps)
            {
                if (isCancelled) return;
                int imageSize = map.diffuseMap.Width * map.diffuseMap.Height;
                auto resultMap = map.indirectLightmap;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    if (map.validPixels.Contains(pixelIdx))
                    {
                        pixelCounter++;
                        if (threadCancelled || (pixelCounter & 15) == 0)
                        {
                            threadCancelled = isCancelled;
                        }
                        if (threadCancelled) continue;
                        int x = pixelIdx % map.diffuseMap.Width;
                        int y = pixelIdx / map.diffuseMap.Width;
                        VectorMath::Vec4 lighting;
                        lighting.SetZero();
                        auto posPixel = map.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                        Random threadRandom(threadRandomSeed);
                        lighting = VectorMath::Vec4::Create(ComputeIndirectLighting(threadRandom, pos, normal, sampleCount), 1.0f);
                        threadRandomSeed = threadRandom.GetSeed();
                        resultMap.SetPixel(x, y, lighting);
                    }
                }
                map.indirectLightmap = _Move(resultMap);
            }
        }

        RawObjectSpaceMap blurTempMap;
        Dictionary<int, List<float>> blurKernels;
        ArrayView<float> GetBlurKernel(int radius)
        {
            int size = radius + 1;
            if (auto rs = blurKernels.TryGetValue(size))
                return rs->GetArrayView();
            List<float> kernel;
            kernel.SetSize(size);
            float sigma = 0.4f;
            float invTwoSigmaSquare = 1.0f / (2.0f * sigma * sigma);
            for (int i = 0; i < size; i++)
            {
                float dist = i / (float)radius;
                float weight = exp(-dist * dist * invTwoSigmaSquare);
                kernel[i] = weight;
            }
            blurKernels[size] = kernel;
            return blurKernels[size]().GetArrayView();
        }
        void BlurIndirectLightmap(IntSet& validPixels, RawObjectSpaceMap & lightmap)
        {
            int blurRadius = Math::Clamp(lightmap.Width / 100, 1, 20);
            blurTempMap.Init(lightmap.GetDataType(), lightmap.Width, lightmap.Height);
            ArrayView<float> kernel = GetBlurKernel(blurRadius);
            #pragma omp parallel for
            for (int y = 0; y < lightmap.Height; y++)
            {
                for (int x = 0; x < lightmap.Width; x++)
                {
                    if (validPixels.Contains(y*lightmap.Width + x))
                    {
                        VectorMath::Vec3 value;
                        float sumWeight = kernel[0];
                        value = lightmap.GetPixel(x, y).xyz();
                        for (int ix = x - 1; ix >= Math::Max(0, x - blurRadius); ix--)
                        {
                            if (validPixels.Contains(y*lightmap.Width + ix))
                            {
                                value += lightmap.GetPixel(ix, y).xyz() * kernel[x-ix];
                                sumWeight += kernel[x - ix];
                            }
                            else
                                break;
                        }
                        for (int ix = x + 1; ix <= Math::Min(lightmap.Width - 1, x + blurRadius); ix++)
                        {
                            if (validPixels.Contains(y*lightmap.Width + ix))
                            {
                                value += lightmap.GetPixel(ix, y).xyz() * kernel[ix - x];
                                sumWeight += kernel[ix - x];
                            }
                            else
                                break;
                        }
                        value *= 1.0f / sumWeight;
                        blurTempMap.SetPixel(x, y, VectorMath::Vec4::Create(value, 1.0f));
                    }
                }
            }

            #pragma omp parallel for
            for (int y = 0; y < lightmap.Height; y++)
            {
                for (int x = 0; x < lightmap.Width; x++)
                {
                    if (validPixels.Contains(y*lightmap.Width + x))
                    {
                        VectorMath::Vec3 value;
                        value = blurTempMap.GetPixel(x, y).xyz();
                        float sumWeight = kernel[0];
                        for (int iy = y - 1; iy >= Math::Max(0, y - blurRadius); iy--)
                        {
                            if (validPixels.Contains(iy*lightmap.Width + x))
                            {
                                value += blurTempMap.GetPixel(x, iy).xyz() * kernel[y - iy];
                                sumWeight += kernel[y - iy];
                            }
                            else
                                break;
                        }
                        for (int iy = y + 1; iy <= Math::Min(lightmap.Height - 1, y + blurRadius); iy++)
                        {
                            if (validPixels.Contains(iy*lightmap.Width + x))
                            {
                                value += blurTempMap.GetPixel(x, iy).xyz() * kernel[iy - y];
                                sumWeight += kernel[iy - y];
                            }
                            else
                                break;
                        }
                        value *= 1.0f / sumWeight;
                        lightmap.SetPixel(x, y, VectorMath::Vec4::Create(value, 1.0f));
                    }
                }
            }
        }

        void CompositeLightmaps()
        {
            lightmaps.Lightmaps.SetSize(maps.Count());
            for (int i = 0; i < maps.Count(); i++)
            {
                auto & lm = lightmaps.Lightmaps[i];
                lm.Init(RawObjectSpaceMap::DataType::RGB32F, maps[i].lightMap.Width, maps[i].lightMap.Height);
                BlurIndirectLightmap(maps[i].validPixels, maps[i].indirectLightmap);
                // composite
                #pragma omp parallel for
                for (int y = 0; y < lm.Height; y++)
                    for (int x = 0; x < lm.Width; x++)
                        lm.SetPixel(x, y, maps[i].lightMap.GetPixel(x, y) + maps[i].indirectLightmap.GetPixel(x, y));

                // dilate
                #pragma omp parallel for
                for (int y = 0; y < lm.Height; y++)
                    for (int x = 0; x < lm.Width; x++)
                    {
                        if (!maps[i].validPixels.Contains(y * lm.Width + x))
                        {
                            for (int iy = Math::Max(y-1, 0); iy <= Math::Min(y + 1, lm.Height-1); iy++)
                                for (int ix = Math::Max(x - 1, 0); ix <= Math::Min(x + 1, lm.Width - 1); ix++)
                                {
                                    if (maps[i].validPixels.Contains(iy * lm.Width + ix))
                                    {
                                        lm.SetPixel(x, y, lm.GetPixel(ix, iy));
                                        goto outContinue;
                                    }
                                }
                        outContinue:;
                        }
                    }
            }
        }
    public:
        std::atomic<bool> isCancelled = false;
        std::atomic<bool> started = false;
        CoreLib::Threading::Thread computeThread;
        void StatusChanged(String status)
        {
            if (!isCancelled)
                OnStatusChanged(status);
        }
        void ProgressChanged(LightmapBakerProgressChangedEventArgs e)
        {
            if (!isCancelled)
                OnProgressChanged(e);
        }
        void ComputeThreadMain()
        {
            StatusChanged("Building BVH...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));

            staticScene = BuildStaticScene(level);
            if (isCancelled) goto computeThreadEnd;
            BiasGBufferPositions();
            if (isCancelled) goto computeThreadEnd;

            StatusChanged("Computing direct lighting...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            ComputeLightmaps_Direct();
            if (isCancelled) goto computeThreadEnd;

            for (int i = 0; i < settings.IndirectLightingBounces; i++)
            {
                StringBuilder statusTextSB;
                statusTextSB << "Computing indirect lighting, pass " << i + 1 << "/" << settings.IndirectLightingBounces;
                if (i == settings.IndirectLightingBounces - 1)
                    statusTextSB << "(final gather)";
                statusTextSB << "...";
                auto statusText = statusTextSB.ToString();
                StatusChanged(statusText);
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
                ComputeLightmaps_Indirect(i == settings.IndirectLightingBounces-1 ? settings.FinalGatherSampleCount : settings.SampleCount);
                CompositeLightmaps();
                if (isCancelled) goto computeThreadEnd;
                OnIterationCompleted();
            }
        computeThreadEnd:;
            if (!isCancelled)
            {
                StatusChanged("Baking completed.");
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
                OnCompleted();
            }
            else
            {
                StatusChanged("Baking cancelled.");
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            }
            started = false;
        }
        virtual void Start(const LightmapBakingSettings & pSettings, Level* pLevel) override
        {
            settings = pSettings;
            level = pLevel;
            StatusChanged("Initializing lightmaps...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            AllocLightmaps();
            BakeLightmapGBuffers();
            started = true;
            isCancelled = false;
            computeThread.Start(new CoreLib::Threading::ThreadProc([this]() 
            {
                ComputeThreadMain();
            }));
        }
        virtual bool IsRunning() override
        {
            return started;
        }
        virtual bool IsCancelled() override
        {
            return isCancelled;
        }
        virtual LightmapSet& GetLightmapSet() override
        {
            return lightmaps;
        }
        virtual void Wait() override
        {
            while (started)
            {
                Engine::Instance()->DoEvents();
            }
            computeThread.Join();
        }
        virtual void Cancel() override
        {
            isCancelled = true;
            Wait();
        }
    };
    LightmapBaker * CreateLightmapBaker()
    {
        return new LightmapBakerImpl();
    }
}