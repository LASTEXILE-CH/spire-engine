#ifndef GAME_ENGINE_WORLD_RENDER_PASS_H
#define GAME_ENGINE_WORLD_RENDER_PASS_H

#include "RenderPass.h"

namespace GameEngine
{
	class WorldRenderPass : public RenderPass
	{
	private:
		FixedFunctionPipelineStates fixedFunctionStates;
		ShaderEntryPoint * vertShader = nullptr, * fragShader = nullptr;
		int renderPassId = -1;
	protected:
		CoreLib::List<CoreLib::RefPtr<AsyncCommandBuffer>> commandBufferPool;
		int poolAllocPtr = 0;
		virtual const char * GetShaderFileName() = 0;
		virtual RenderTargetLayout * CreateRenderTargetLayout() = 0;
		virtual void SetPipelineStates(FixedFunctionPipelineStates & state)
		{
			state.blendMode = BlendMode::Replace;
			state.DepthCompareFunc = CompareFunc::Less;
		}
		virtual void Create(Renderer * renderer) override;
	public:
		~WorldRenderPass();
		void ResetInstancePool()
		{
			poolAllocPtr = 0;
		}
		virtual void Bind();
		AsyncCommandBuffer * AllocCommandBuffer();
		CoreLib::RefPtr<WorldPassRenderTask> CreateInstance(RenderOutput * output, bool clearOutput);
		virtual int GetShaderId() override;
	};
}

#endif
