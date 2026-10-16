#pragma once

#include "Math/EngineMath.h"
#include "Math/Sphere.h"
#include "Engine/SceneManagement.h"
#include "MeshPassProcessor.h"

class FPrimitiveSceneProxy;
struct FLightSceneInfo;
class FSceneRenderer;

// 그림자 맵 한 장. 프레임마다 FSceneRenderData가 만들고 비운다.
class FProjectedShadowInfo
{
public:
	// 어느 라이트의 그림자인지
	FLightSceneInfo* LightSceneInfo = nullptr;

	FVector PreShadowTranslation;
	FMatrix ShadowView, ShadowProjection, ShadowViewProjection;
	FSphere ShadowBounds{ FVector::ZeroVector, 0.0f };
	FShadowCascadeSettings CascadeSettings;
	float MinSubjectZ = 0.0f;
	float MaxSubjectZ = 0.0f;
	// Shadow Map 안의 위치 (3단계 배정에서 채움)
	uint32 X = 0, Y = 0, ResolutionX = 0, ResolutionY = 0, BorderSize = 0;
	bool bAllocated = false;
	// 종류
	bool bWholeSceneShadow = false, bDirectionalLight = false, bOnePassPointLightShadow = false;
	// Caster
	TArray<FPrimitiveSceneProxy*> DynamicSubjectPrimitives;
	TArray<FMeshDrawBatch> ShadowDepthBatches;   // 언리얼의 ShadowDepthPass 역할

	FFrustumPlanes CasterFrustum{};

	void SetupWholeSceneProjection(FLightSceneInfo* InLightSceneInfo,
		const FWholeSceneProjectedShadowInitializer& Initializer,
		uint32 InResolutionX, uint32 InResolutionY, uint32 InBorderSize);
	void AddSubjectPrimitive(FPrimitiveSceneProxy* Proxy) { DynamicSubjectPrimitives.Add(Proxy); }
	float GetShaderDepthBias() const;
	float GetShaderSlopeDepthBias() const;

	void SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer);
};
