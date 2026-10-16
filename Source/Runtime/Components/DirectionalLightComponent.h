#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(DynamicShadowDistanceMovableLight)
		PROPERTY(DynamicShadowCascades)
		PROPERTY(CascadeDistributionExponent)
		PROPERTY(CascadeTransitionFraction)
		PROPERTY(ShadowDistanceFadeoutFraction)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }


private:
	float DynamicShadowDistanceMovableLight = 1.0f;
	uint32 DynamicShadowCascades = 3;
	float CascadeDistributionExponent = 3.0f;
	float CascadeTransitionFraction = 0.1f;
	float ShadowDistanceFadeoutFraction = 0.1f;
};