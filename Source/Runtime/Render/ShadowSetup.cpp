#include "EnginePCH.h"
#include "ShadowRendering.h"
#include "SceneRenderer.h"

void FProjectedShadowInfo::SetupWholeSceneProjection(FLightSceneInfo* InLightSceneInfo, const FWholeSceneProjectedShadowInitializer& Initializer, uint32 InResolutionX, uint32 InResolutionY, uint32 InBorderSize)
{
	LightSceneInfo = InLightSceneInfo;
	PreShadowTranslation = Initializer.PreShadowTranslation;
	ResolutionX = InResolutionX;
	ResolutionY = InResolutionY;
	BorderSize = InBorderSize;
	bWholeSceneShadow = true;
	bOnePassPointLightShadow = Initializer.bOnePassPointLightShadow;
	CascadeSettings = Initializer.CascadeSettings;

	// 깊이 범위: Spot은 near = MinLightW, far = MaxDistanceToCastInLightW
	MinSubjectZ = Initializer.MinLightW;
	MaxSubjectZ = Initializer.MaxDistanceToCastInLightW;

	// View: "라이트를 원점으로 옮기고(PreShadowTranslation) → 라이트 방향으로 돌린다(WorldToLight)"
	// 엔진 투영 행렬이 X를 앞쪽으로 받으므로 언리얼의 FaceMatrix는 곱하지 않는다.
	ShadowView = FMatrix::MakeTranslation(PreShadowTranslation) * Initializer.WorldToLight;

	// Projection: Scales = 1 / tan(반각) → FOV(도) = 2 * atan(1 / Scales)
	const float FOV = FMath::RadiansToDegrees(2.0f * std::atan(1.0f / Initializer.Scales.X));
	ShadowProjection = FMatrix::MakePerspectiveLH(FOV, 1.0f, MinSubjectZ, MaxSubjectZ);

	ShadowViewProjection = ShadowView * ShadowProjection;   // 카메라와 같은 View * Proj 순서
	CasterFrustum = ExtractFrustumPlanes(ShadowViewProjection);

	// 언리얼(Directional이 아닐 때): 라이트 위치 중심, SubjectBounds 반지름
	ShadowBounds = FSphere(PreShadowTranslation * -1.0f, Initializer.SubjectBounds.SphereRadius);
}

void FProjectedShadowInfo::SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer)
{
	FSceneView ShadowSceneView = Renderer.GetView();
	ShadowSceneView.ViewMatrix = ShadowView;
	ShadowSceneView.ProjectionMatrix = ShadowProjection;
	ShadowSceneView.ViewProjectionMatrix = ShadowViewProjection;
	ShadowSceneView.Frustum = CasterFrustum;
	ShadowSceneView.ViewLocation = PreShadowTranslation * -1.0f;   // 라이트 위치
	ShadowSceneView.bIsWireframe = false;

	FRenderQueue ShadowQueue;
	for (FPrimitiveSceneProxy* Proxy : DynamicSubjectPrimitives)
	{
		if (!Proxy->GetMesh() || !Proxy->GetRenderState()) continue;
		const uint32 LOD = 0;                                        // 처음엔 LOD0 고정
		const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
		for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
		{
			const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
			// 반투명 재질은 그림자를 드리우지 않음 (불투명만)
			if (!Section.Material || Section.Material->PSOType != EPSOType::StaticMesh_Opaque) continue;

			FRenderPacket& Packet = ShadowQueue.AddDefaulted_GetRef();
			Packet.Proxy = Proxy;
			Packet.Mesh = Proxy->GetMesh();
			Packet.Material = Section.Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.LODIndex = static_cast<uint8>(LOD);
			Packet.Slot = InvalidObjectSlot;                      // 물체별 상수를 직접 넣으므로 슬롯 불필요
					}
	}
	ShadowQueue.Sort();   // 필수: FirstTranslucentIndex를 계산한다

	// 3. 그림자 깊이용 그릴 목록
	FMeshPassProcessor Processor;
	Processor.Build(ShadowSceneView, ShadowQueue, /*SceneData*/ nullptr,
		EMeshPassType::CSMShadowDepth, ShadowDepthBatches, ELightingModel::Lambert);
}
