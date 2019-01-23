#ifndef ANIMATION_SYNTHESIZER_H
#define ANIMATION_SYNTHESIZER_H

#include "Skeleton.h"
#include "CoreLib/LibMath.h"
#include "CatmullSpline.h"
#include <queue>
#include <stack>
#include <ctime>

namespace GameEngine
{
	class AnimationSynthesizer : public CoreLib::RefObject
	{	
    public:
		virtual void GetPose(Pose & p, float time) = 0;
	};

	class SimpleAnimationSynthesizer : public AnimationSynthesizer
	{
	private:
		Skeleton * skeleton = nullptr;
		SkeletalAnimation * anim = nullptr;
	public:
		SimpleAnimationSynthesizer() = default;
		SimpleAnimationSynthesizer(Skeleton * pSkeleton, SkeletalAnimation * pAnim)
			: skeleton(pSkeleton), anim(pAnim)
		{}
		void SetSource(Skeleton * pSkeleton, SkeletalAnimation * pAnim)
		{
			this->skeleton = pSkeleton;
			this->anim = pAnim;
		}
		virtual void GetPose(Pose & p, float time) override;
	};
}

#endif