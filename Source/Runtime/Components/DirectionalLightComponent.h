#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(SourceAngle)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	inline float GetSourceAngle() const { return SourceAngle; }
	inline void SetSourceAngle(float SourceAngle) { this->SourceAngle = SourceAngle; }

	ELightType GetLightType() const override { return ELightType::Directional; }

private:
	float SourceAngle = 0.5357f;
};