#pragma once

#include "Math/EngineMath.h"
#include "Math/Color.h"
#include <Components/LightComponentBase.h>

// 모든 광원(방향광·포인트·스포트·렉트)이 공유하는 GPU 원소.
// LightData.hlsli의 FLightRenderParameters와 바이트 단위로 같아야 한다.
struct FLightRenderParameters
{
	FVector Position;
	float Radius;
	FVector Color;
	float Intensity;
	FVector Direction;
	float InnerConeCos;
	FVector Right;
	float FalloffExponent;
	float OuterConeCos;
	float HalfWidth;
	float HalfHeight;
	uint32 Type;
	// Rect 전용: 발광면 가장자리의 문(Barn Door)
	float BarnDoorCos;
	float BarnDoorSin;
	float BarnDoorLength;
	
	bool bCastShadows = false;
	int32 ShadowViewBaseIndex = -1;
	uint32 ShadowViewCount = 0;
	FVector2 Padding;
};

static_assert(sizeof(FLightRenderParameters) == 112, "sizeof(LightData.hlsli FLightRenderParameters) == sizeof(FLightRenderParameters)");

struct FShadowAtlasRect
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Width = 0;
	uint32 Height = 0;
};

struct FShadowViewData
{
	uint32 LightIndex = 0;
	uint32 FaceIndex = 0;
	FShadowAtlasRect AtlasRect;
	FMatrix LightVP;
	float ConstantBias = 0.0005f;
	float SlopeBias = 0.002f;
};

struct alignas(16) FShadowViewGPUData
{
	FMatrix LightVP;

	// XY = UV Scale, ZW = UV Bias
	FVector4 AtlasScaleBias;

	// X = Constant Bias, Y = Slope Bias
	FVector2 ShadowParams;

	FVector2 Padding;
};

struct alignas(16) FShadowViewConstantBuffer
{
	FShadowViewGPUData Views[16];
	uint32 ViewCount = 0;
	FVector Padding;
};

struct alignas(16) FShadowDepthViewConstants
{
	FMatrix LightVP;
};

constexpr uint32 ShadowAtlasResolution = 2048;
constexpr uint32 ShadowTileResolution = 512;
constexpr uint32 ShadowAtlasColumns = ShadowAtlasResolution / ShadowTileResolution;
constexpr uint32 MaxShadowViews = ShadowAtlasColumns * ShadowAtlasColumns;

static FShadowAtlasRect GetShadowAtlasRect(uint32 Slot)
{
	assert(Slot < MaxShadowViews);

	FShadowAtlasRect Rect;
	Rect.X = (Slot % ShadowAtlasColumns) * ShadowTileResolution;
	Rect.Y = (Slot / ShadowAtlasColumns) * ShadowTileResolution;
	Rect.Width = ShadowTileResolution;
	Rect.Height = ShadowTileResolution;

	return Rect;
}

static uint32 GetRequiredShadowViewCount(ELightType LightType)
{
	switch (LightType)
	{
	case ELightType::Directional:
	case ELightType::Spot:
		return 1;
		break;
	case ELightType::Point:
		return 6;
		break;
	case ELightType::Rect:
	case ELightType::Fireball:
	case ELightType::Sky:
	default:
		return 0;
		break;
	}
}