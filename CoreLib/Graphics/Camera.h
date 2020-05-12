#ifndef GX_GL_CAMERA_H
#define GX_GL_CAMERA_H
#include "../VectorMath.h"
#include "ViewFrustum.h"
namespace CoreLib
{
	namespace Graphics
	{
		class Camera
		{
		public:
			Camera();
		public:
			float alpha,beta;
			VectorMath::Vec3 pos,up,dir;
			bool CanFly;
			void GoForward(float u);
			void MoveLeft(float u);
			void TurnLeft(float u);
			void TurnUp(float u);
			void GetTransform(VectorMath::Matrix4 & mat);
			void GetView(ViewFrustum & view);
			void Reset();
			void GetInverseRotationMatrix(float rot[9]);
		};
	}
}
#endif