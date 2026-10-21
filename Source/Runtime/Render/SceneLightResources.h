#pragma once

#include "Render/LightRenderData.h"
#include "Render/ForwardLightTiles.h"
#include "Render/Buffer.h"
#include "Render/Texture2D.h"
#include "Containers/Array.h"

struct FDeferredLightConstants
{
	FMatrix InverseViewProjection;
	FVector4 CameraPosition;
	FVector4 CameraForward;
	FVector4 ViewportSize;
	FAmbientLightRenderData Ambient;
	uint32 LightCounts[4];
	uint32 LightOffsets[4];
	uint32 LightShadowOffsets[4];
	FVector4 SkyColorIntensity;
	uint32 ShadowAtlasWidth;
	uint32 ShadowAtlasHeight;
	uint32 CascadeShadowCount = 0;
	uint32 PcfMode = 0;
	float CascadeShadowDistances[4]{};
};

// 뷰별 조명 GPU 리소스. 패스나 드로우는 실행하지 않는다.
class FSceneLightResources
{
public:
	bool Init();
	bool CreateShadowAtlasIfNeed(uint32 ShadowAtlasMapWidth, uint32 ShadowAtlasMapHeight, uint32 NeedAtlasCount, bool& bOutRecreated);

	// 광원을 종류별 구간(방향광, 포인트, 스포트, 렉트 순)으로 정렬해 업로드한다.
	// Counts/Offsets 순서는 x: 포인트, y: 스포트, z: 방향광, w: 렉트다.
	bool UploadViewLights(const TArray<FLightRenderParameters>& Lights);
	bool UploadLightShadows(const TArray<FLightShadowRenderData>& Shadows);

	FStructuredBuffer* GetSceneLights() const { return SceneLightBuffer.get(); }
	FTexture2DArray* GetShadowMapAtlas() const { return SceneLightShadowMapAtlas.get(); }
	FStructuredBuffer* GetLightShadows() const { return SceneLightShadowBuffer.get(); }

	FConstantBuffer* GetForwardConstants() const { return ForwardLightCB.get(); }
	FConstantBuffer* GetDeferredConstants() const { return DeferredLightCB.get(); }

	FForwardLightTileGrid& GetTiles() { return ForwardTileGrid; }

private:
	TUniquePtr<FStructuredBuffer> SceneLightBuffer;

	// NOTE: Shadow Map 아틀라스와 해당 영역 데이터 저장
	TUniquePtr<FTexture2DArray> SceneLightShadowMapAtlas;
	TUniquePtr<FStructuredBuffer> SceneLightShadowBuffer;

	TUniquePtr<FConstantBuffer> ForwardLightCB;
	TUniquePtr<FConstantBuffer> DeferredLightCB;

	FForwardLightTileGrid ForwardTileGrid;
	bool bInitialized = false;
};
