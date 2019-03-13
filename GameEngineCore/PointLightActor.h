#ifndef GAME_ENGINE_POINT_LIGHT_ACTOR
#define GAME_ENGINE_POINT_LIGHT_ACTOR

#include "LightActor.h"
#include "CoreLib/VectorMath.h"

namespace GameEngine
{
	class PointLightActor : public LightActor
	{
	protected:
		virtual Mesh CreateGizmoMesh() override;
	public:
		PROPERTY_DEF(bool, IsSpotLight, false);
		PROPERTY_DEF(VectorMath::Vec3, Color, VectorMath::Vec3::Create(1.0f, 1.0f, 1.0f));
		PROPERTY_DEF(float, SpotLightStartAngle, 0.0f);
		PROPERTY_DEF(float, SpotLightEndAngle, 90.0f);
		virtual CoreLib::String GetTypeName() override
		{
			return "PointLight";
		}
		PointLightActor()
		{
			lightType = LightType::Point;
		}

	};
}

#endif