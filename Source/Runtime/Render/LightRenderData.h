#pragma once

#include "Math/Vector4.h"
#include "Math/Rect.h"
#include "Core/EngineString.h"
#include "Engine/SceneManagement.h"
#include "Math/Rect.h"

#include <cstddef>

// CPU의 그림자 그리기와 GPU 업로드에서 공유하는 데이터.
struct FShadowParameters 
{
	FMatrix LightVP = FMatrix::Identity;
	FRect AtlasRect{}; // 픽셀 단위 위치와 크기
	uint32 AtlasIndex = UINT32_MAX; // 영역 배정 전에는 유효한 atlas가 없다.
	uint32 Padding[3]{};
};

struct FLightShadowRange // point light 때문에 생긴 구조체
{
	uint32 StartIndex = 0; 
	uint32 ViewCount = 0; 
};

struct FShadowConstants
{
	uint32 ShadowsEnabled = 0;
	float ShadowDepthBias = 0.0001f;
	uint32 Padding[2]{};
};

static_assert(sizeof(FShadowParameters) == 96);
static_assert(offsetof(FShadowParameters, AtlasRect) == 64);
static_assert(offsetof(FShadowParameters, AtlasIndex) == 80);
static_assert(sizeof(FLightShadowRange) == 8);
static_assert(sizeof(FShadowConstants) == 16);

// UberLit.hlsl의 FAmbientLightInfo와 같은 배치다.
struct FAmbientLightRenderData
{
	FVector4 ColorIntensity;
};

// Sky Light 설정의 씬 스냅샷. SourcePath로 캐시된 irradiance 큐브맵을 찾는다.
struct FSkyLightRenderData
{
	FVector4 ColorIntensity;
	FString SourcePath;
};

// UberLit.hlsl의 b1 조명 상수 버퍼와 같은 배치다.
struct FForwardLightingConstants
{
	FAmbientLightRenderData Ambient;
	FVector4 CameraPosition;
	uint32 LightCounts[4];  // x: 포인트 개수, y: 스포트 개수, z: 방향광 개수, w: 렉트 개수
	uint32 LightOffsets[4]; // 공유 조명 버퍼에서 각 종류가 시작되는 위치, 순서는 위와 같음
	uint32 LightShadowOffsets[4]; // 공유 그림자 버퍼에서 각 종류가 시작되는 위치
	FVector4 SkyColorIntensity;
	uint32 ShadowAtlasWidth;
	uint32 ShadowAtlasHeight;
	uint32 Padding[2];
};

struct FLightShadowRenderData
{
	FMatrix ViewProjection;
	FRect AtlasRect;
	uint32 AtlasIndex;

	float CascadeFarDepth = 0.0f;

	uint32 Padding[2]{};
};

static_assert(sizeof(FVector4) == 16);
static_assert(sizeof(FAmbientLightRenderData) == 16);
static_assert(offsetof(FForwardLightingConstants, CameraPosition) == 16);
static_assert(offsetof(FForwardLightingConstants, LightCounts) == 32);
static_assert(offsetof(FForwardLightingConstants, LightOffsets) == 48);
static_assert(offsetof(FForwardLightingConstants, LightShadowOffsets) == 64);
static_assert(offsetof(FForwardLightingConstants, SkyColorIntensity) == 80);
static_assert(offsetof(FForwardLightingConstants, ShadowAtlasWidth) == 96);
static_assert(offsetof(FForwardLightingConstants, ShadowAtlasHeight) == 100);
static_assert(sizeof(FForwardLightingConstants) == 112);
