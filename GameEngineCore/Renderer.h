#ifndef GAME_ENGINE_RENDERER_H
#define GAME_ENGINE_RENDERER_H

#include "CoreLib/Basic.h"
#include "OS.h"

namespace GameEngine
{
	class Texture2D;
	class HardwareRenderer;
	class WorldRenderPass;
	class PostRenderPass;
	class RendererSharedResource;
	class SceneResource;
	class Material;
	class ModuleInstance;
	class Level;
	class RenderStat;
    class RendererService;
    struct LightmapSet;
	class Renderer : public CoreLib::Object
	{
	public:
		virtual int RegisterWorldRenderPass(uint32_t shaderId) = 0;
		virtual void UpdateLightProbes() = 0;
		virtual void DestroyContext() = 0;
		virtual void InitializeLevel(Level * level) = 0;
		virtual void TakeSnapshot() = 0;
		virtual RenderStat& GetStats() = 0;
		virtual void RenderFrame() = 0;
		virtual void Resize(int w, int h) = 0;
		virtual void Wait() = 0;
		virtual SceneResource * GetSceneResource() = 0;
		virtual RendererSharedResource * GetSharedResource() = 0;
		virtual Texture2D * GetRenderedImage() = 0;
		virtual HardwareRenderer * GetHardwareRenderer() = 0;
        virtual RendererService* GetRendererService() = 0;
        virtual void UpdateLightmap(LightmapSet& lightmapSet) = 0;
	};

	Renderer* CreateRenderer(RenderAPI api);
}

#endif