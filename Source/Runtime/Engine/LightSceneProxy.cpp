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

void FRectLightSceneProxy::GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const
{
}

void FDirectionalLightSceneProxy::GetShadowShaderParameters(FShadowViewData& OutShadowViewData, uint32 Index) const
{
	const UDirectionalLightComponent* Dir = static_cast<const UDirectionalLightComponent*>(LightComponent);

	FTransform Transform;
	Transform.Rotation = Dir->GetWorldRotation();

	const FVector Center = { 0,0,0 };
	const float Radius = 10000.0f;
	const float Padding = std::max(10.0f, Radius * 0.1f);

	const FVector CameraPosition = Center - (Transform.GetForward() * (Radius + Padding));
	const FVector Up = Transform.GetUp();

	const float Width = 2.0f * (Radius + Padding);
	const float Height = Width;
	const float NearZ = 0.1f;
	const float FarZ = 2.0f * Radius + 2.0f * Padding;

	FQuat WorldQuat = Transform.Rotation.Quaternion();
	FMatrix RotationMatrix = WorldQuat.ToFMatrix();
	const FMatrix InverseTranslation = FMatrix::MakeTranslation(CameraPosition * -1.0f);
	FMatrix InverseRotation = RotationMatrix.GetTransposed();

	FMatrix View = InverseTranslation * InverseRotation;

	const float XScale = 2.0f / Width;
	const float YScale = 2.0f / Height;

	const float ZScale = 1.0f / (FarZ - NearZ);
	const float ZOffset = -NearZ / (FarZ - NearZ);

	const FMatrix Projection = { 0.0f, 0.0f, ZScale, 0.0f, XScale, 0.0f, 0.0f, 0.0f, 0.0f, YScale, 0.0f, 0.0f, 0.0f, 0.0f, ZOffset, 1.0f };

	OutShadowViewData.LightVP = View * Projection;
}

void FPointLightSceneProxy::GetShadowShaderParameters(FShadowViewData& OutShadowViewData, uint32 Index) const
{
	const auto* PointLightComponent = static_cast<const UPointLightComponent*>(LightComponent);

}

void FSpotLightSceneProxy::GetShadowShaderParameters(FShadowViewData& OutShadowViewData, uint32 Index) const
{
	const auto* SpotLightComponent = static_cast<const USpotLightComponent*>(LightComponent);
	const FLinearColor Color(SpotLightComponent->GetLightColor());

	FTransform Transform;
	Transform.Rotation = SpotLightComponent->GetWorldRotation();

	const float Outer = FMath::Clamp(SpotLightComponent->GetOuterConeAngle(), 0.0f, 89.0f);
	const float Inner = FMath::Clamp(SpotLightComponent->GetInnerConeAngle(), 0.0f, Outer);

	float Radius = SpotLightComponent->GetAttenuationRadius();

	const FVector Up = Transform.GetUp();

	FQuat WorldQuat = Transform.Rotation.Quaternion();
	FMatrix RotationMatrix = WorldQuat.ToFMatrix();
	const FMatrix InverseTranslation = FMatrix::MakeTranslation(SpotLightComponent->GetWorldLocation() * -1.0f);
	FMatrix InverseRotation = RotationMatrix.GetTransposed();

	FMatrix View = InverseTranslation * InverseRotation;

	const float HalfFOV = FMath::DegreesToRadians(Outer);

	const float YScale = 1.0f / tan(HalfFOV);
	const float XScale = YScale / 1;

	const float ZScale = Radius / (Radius - 0.1f);
	const float ZOffset = -0.1f * Radius / (Radius - 0.1f);

	const FMatrix Projection = { 0.0f, 0.0f, ZScale, 1.0f, XScale, 0.0f, 0.0f, 0.0f, 0.0f, YScale, 0.0f, 0.0f, 0.0f, 0.0f, ZOffset, 0.0f };

	OutShadowViewData.LightVP = View * Projection;
}