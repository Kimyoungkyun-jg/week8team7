#include "EnginePCH.h"
#include "LightSceneProxy.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"

void FDirectionalLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const UDirectionalLightComponent* Dir = static_cast<const UDirectionalLightComponent*>(LightComponent);

	FTransform Transform;
	Transform.Rotation = Dir->GetWorldRotation();
	const FLinearColor Color(Dir->GetLightColor());

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Directional);
	Out.Direction = Transform.GetForward();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = Dir->GetIntensity();
}

void FPointLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const auto* PointLightComponent = static_cast<const UPointLightComponent*>(LightComponent);
	const FLinearColor Color(PointLightComponent->GetLightColor());

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Point);
	Out.Position = PointLightComponent->GetWorldLocation();
	Out.Radius = PointLightComponent->GetAttenuationRadius();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = PointLightComponent->GetIntensity();
	Out.FalloffExponent = PointLightComponent->GetFalloff();
}

void FSpotLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const auto* SpotLightComponent = static_cast<const USpotLightComponent*>(LightComponent);
	const FLinearColor Color(SpotLightComponent->GetLightColor());

	FTransform Transform;
	Transform.Rotation = SpotLightComponent->GetWorldRotation();

	const float Outer = FMath::Clamp(SpotLightComponent->GetOuterConeAngle(), 0.0f, 89.0f);
	const float Inner = FMath::Clamp(SpotLightComponent->GetInnerConeAngle(), 0.0f, Outer);

	Out = FLightRenderParameters{};
	Out.Position = SpotLightComponent->GetWorldLocation();
	Out.Radius = SpotLightComponent->GetAttenuationRadius();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = SpotLightComponent->GetIntensity();
	Out.FalloffExponent = SpotLightComponent->GetFalloff();

	Out.Type = static_cast<uint32>(ELightType::Spot);
	Out.Direction = Transform.GetForward();
	Out.InnerConeCos = std::cos(FMath::DegreesToRadians(Inner));
	Out.OuterConeCos = std::cos(FMath::DegreesToRadians(Outer));
}

bool FSpotLightSceneProxy::GetWholeSceneProjectedShadowInitializer(const FSceneView& ViewFamily, TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const
{
	const USpotLightComponent* Spot = Cast<const USpotLightComponent>(LightComponent);

	const FVector LightPos = Spot->GetWorldLocation();
	const float Radius = Spot->GetAttenuationRadius();
	const float Outer = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);
	const float InvTan = 1.0f / tanf(FMath::DegreesToRadians(Outer));

	FWholeSceneProjectedShadowInitializer& OutInitializer = OutInitializers.AddDefaulted_GetRef();
	OutInitializer.PreShadowTranslation = -LightPos;
	OutInitializer.WorldToLight = Spot->GetWorldRotation().Quaternion().ToFMatrix().GetTransposed();
	OutInitializer.Scales = FVector2(InvTan, InvTan);
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(Radius), Radius);
	OutInitializer.WAxis = FVector4(0, 0, 1, 0);
	OutInitializer.MinLightW = 0.1f;
	OutInitializer.MaxDistanceToCastInLightW = Radius;

	return true;
}

void FRectLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const auto* RectLightComponent = static_cast<const URectLightComponent*>(LightComponent);
	const FLinearColor Color(RectLightComponent->GetLightColor());

	FTransform Transform;
	Transform.Rotation = RectLightComponent->GetWorldRotation();

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Rect);
	Out.Position = RectLightComponent->GetWorldLocation();
	Out.Radius = RectLightComponent->GetAttenuationRadius();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = RectLightComponent->GetIntensity();
	Out.FalloffExponent = 1.0f;
	Out.Direction = Transform.GetForward();
	Out.Right = Transform.GetRight();
	Out.HalfWidth = RectLightComponent->GetWidth() * 0.5f;
	Out.HalfHeight = RectLightComponent->GetHeight() * 0.5f;

	const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(RectLightComponent->GetBarnDoorAngle(), 0.0f, 90.0f));
	Out.BarnDoorCos = std::cos(AngleRad);
	Out.BarnDoorSin = std::sin(AngleRad);
	Out.BarnDoorLength = std::max(RectLightComponent->GetBarnDoorLength(), 0.0f);
}
