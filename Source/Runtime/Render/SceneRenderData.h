#pragma once

#include "Render/RenderPacket.h"
#include "Render/ObjectConstants.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/LightSceneProxy.h"
#include "Render/LightRenderData.h"
#include "Render/SceneLightResources.h"
#include "Math/Matrix.h"
#include <algorithm>
#include <iterator>
#include <vector>
#include "Render/SceneView.h"

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	void Reset() { *this = FRenderStats(); }
};

// 뷰 하나의 데이터를 프레임 사이에도 보관한다. SceneRenderer는 현재 프레임 동안 이를 빌려 쓴다.
struct FSceneRenderData
{
	static constexpr int32 PointLightSlotIndex = 0;
	static constexpr int32 SpotLightSlotIndex = 1;
	static constexpr int32 DirectionalLightSlotIndex = 2;
	static constexpr int32 RectLightSlotIndex = 3;
	
	static constexpr uint32 ShadowMapSize = 4096;
	static constexpr uint32 DirectionalCascadeCount = 4;
	static constexpr uint32 DirectionalLightShadowMapSize = 2048;
	static constexpr uint32 PointLightShadowMapSize = 1024;
	static constexpr uint32 SpotLightShadowMapSize = 512;

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets;              // 스태틱 묶음에 못 들어가는 것 (반투명 섹션, Renderer 없는 호출)
		TArray<uint32> SlowPathIndices;
		std::vector<FStaticDrawGroup> Groups;       // 불투명 스태틱 메시: (머티리얼, 메시, LOD)별 묶음. 항목만 매 프레임 비운다
		uint32 LastGroup = 0;                       // 바로 전 물체가 들어간 묶음 (연속한 물체는 대개 같은 묶음)
		uint32 StaticDrawCount = 0;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};

	FRenderQueue RenderQueue;
	FRenderStats RenderStats;
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;
	TArray<FGatherChunk> GatherChunks;

	TArray<FPerObjectConstants> ObjectConstants;
	FSceneLightResources ViewLightResources;

	FAmbientLightRenderData AmbientLight{};
	FSkyLightRenderData SkyLight{};
	bool bHasSkyLight = false;

	TArray<FLightRenderParameters> VisibleLights; // 모든 종류의 광원. 업로드할 때 종류별 구간으로 정렬한다
	uint32 LightParameterOffsets[4]; // 라이트 리스트 내 구간의 offset
	uint32 LightParameterCounts[4]; // 라이트 리스트 내 구간의 count

	TArray<FLightShadowRenderData> LightShadows;
	uint32 LightShadowOffsets[4];
	float DirectionalCascadeSplits[DirectionalCascadeCount] = {};

	void ResetScene()
	{
		ResetFrame();
		for (FGatherChunk& Chunk : GatherChunks)
		{
			Chunk.Groups.clear();
			Chunk.LastGroup = 0;
		}
	}

	void ResetFrame()
	{
		RenderQueue.Reset();
		RenderStats.Reset();
		VisibleProxies.Reset();
		LODInputs.Reset();
		SelectedLODs.Reset();
		ObjectConstants.Reset();
		AmbientLight = {};
		SkyLight = {};
		bHasSkyLight = false;
		VisibleLights.Reset();
		LightShadows.Reset();
		std::fill(std::begin(DirectionalCascadeSplits), std::end(DirectionalCascadeSplits), 0.0f);
		std::memset(LightParameterOffsets, 0, sizeof(LightParameterOffsets));
		std::memset(LightParameterCounts, 0, sizeof(LightParameterCounts));

		for (FGatherChunk& Chunk : GatherChunks)
		{
			Chunk.Packets.Reset();
			Chunk.SlowPathIndices.Reset();
			Chunk.StaticDrawCount = 0;
			std::fill(std::begin(Chunk.LODCounts), std::end(Chunk.LODCounts), 0u);
			std::fill(std::begin(Chunk.LODTriangles), std::end(Chunk.LODTriangles), 0ull);
			for (FStaticDrawGroup& Group : Chunk.Groups) Group.Items.clear();
		}
	}

	void Commit(const FSceneView& View, bool& bLightUpaloadSucceeded)
	{
		RenderQueue.Sort();

		uint32 AtlasNeedCount = 1;
		if (VisibleLights.Num())
		{
			constexpr uint32 TypeCount = 4;
			constexpr uint32 TypeToSlot[TypeCount] = {
				FSceneRenderData::DirectionalLightSlotIndex,
				FSceneRenderData::PointLightSlotIndex,
				FSceneRenderData::SpotLightSlotIndex,
				FSceneRenderData::RectLightSlotIndex,
			};

			std::sort(VisibleLights.GetData(), VisibleLights.GetData() + VisibleLights.Num(), [&TypeCount, &TypeToSlot](const FLightRenderParameters& A, const FLightRenderParameters& B) {
				const uint32 ASlot = TypeToSlot[A.Type];
				const uint32 BSlot = TypeToSlot[B.Type];
				return ASlot < BSlot;
			});

			// Light 배열 정리 및 구간 별 Offset, Count 기록
			int32 Offset = 0;
			int32 Count = 1;
			for (int32 i = 1; i < VisibleLights.Num(); i++)
			{
				const FLightRenderParameters& LightParams = VisibleLights[i];
				const FLightRenderParameters& PrevLightParams = VisibleLights[i - 1];

				if (LightParams.Type != PrevLightParams.Type)
				{
					int32 Slot = TypeToSlot[PrevLightParams.Type];
					LightParameterOffsets[Slot] = Offset;
					LightParameterCounts[Slot] = Count;

					Offset += Count;
					Count = 1;
				}
				else
				{
					Count++;
				}
			}

			const FLightRenderParameters& LightParams = VisibleLights.Last();
			int32 Slot = TypeToSlot[LightParams.Type];
			LightParameterOffsets[Slot] = Offset;
			LightParameterCounts[Slot] = Count;

			// Light의 View, Projection 아틀라스 영역의 데이터를 저장한다.
			struct FAreaInfo
			{
				uint32 Size;
				uint32 Index;
			};

			TArray<FAreaInfo> AreaInfos;

			uint32 TotalAreaSize = 0;
			uint32 MaxAreaSize = 0;
			uint32 ShadowMapOffset = 0;

			LightShadowOffsets[FSceneRenderData::PointLightSlotIndex] = ShadowMapOffset;
			for (int32 i = 0; i < GetPointLightCount(); i++)
			{
				int32 LightIndex = GetPointLightOffset() + i;

				const FLightRenderParameters& PointLight = VisibleLights[LightIndex];

				FMatrix PerspectiveMatrix = Perspective(FMath::DegreesToRadians(90.f), 1.0, 0.1f, PointLight.Radius);
				FMatrix Views[] = {
					LookAt(PointLight.Position, PointLight.Position + ForwardVector, UpVector),
					LookAt(PointLight.Position, PointLight.Position + BackwardVector, UpVector),
					LookAt(PointLight.Position, PointLight.Position + UpVector, BackwardVector),
					LookAt(PointLight.Position, PointLight.Position + DownVector, ForwardVector),
					LookAt(PointLight.Position, PointLight.Position + RightVector, UpVector),
					LookAt(PointLight.Position, PointLight.Position + LeftVector, UpVector),
				};

				for (int32 j = 0; j < 6; j++)
				{
					FLightShadowRenderData Shadow{};
					Shadow.ViewProjection = Views[j] * PerspectiveMatrix;
					
					uint32 Index = LightShadows.Add(Shadow);

					AreaInfos.Add({ PointLightShadowMapSize, Index });

					TotalAreaSize += PointLightShadowMapSize * PointLightShadowMapSize;
					MaxAreaSize = FMath::Max(MaxAreaSize, PointLightShadowMapSize);
					ShadowMapOffset++;
				}
			}

			LightShadowOffsets[FSceneRenderData::SpotLightSlotIndex] = ShadowMapOffset;
			for (int32 i = 0; i < GetSpotLightCount(); i++)
			{
				int32 LightIndex = GetSpotLightOffset() + i;

				const FLightRenderParameters& SpotLight = VisibleLights[LightIndex];

				FMatrix ViewMatrix = LookAt(SpotLight.Position, SpotLight.Position + SpotLight.Direction, UpVector);
				FMatrix ProjectionMatrix = Perspective(std::acos(SpotLight.OuterConeCos) * 2.0f, 1.0, 0.1f, SpotLight.Radius);

				FLightShadowRenderData Shadow{};
				Shadow.ViewProjection = ViewMatrix * ProjectionMatrix;
				
				uint32 Index = LightShadows.Add(Shadow);

				AreaInfos.Add({ SpotLightShadowMapSize, Index });

				TotalAreaSize += SpotLightShadowMapSize * SpotLightShadowMapSize;
				MaxAreaSize = FMath::Max(MaxAreaSize, SpotLightShadowMapSize);
				ShadowMapOffset++;
			}

			if (GetDirectionalLightCount() > 0)
			{
				constexpr float CascadeSplitLambda = 0.95f;

				const float NearClip = View.NearClip;
				const float FarClip = View.FarClip;

				assert(NearClip > 0.0f);
				assert(FarClip > NearClip);

				for (uint32 CascadeIndex = 0; CascadeIndex < DirectionalCascadeCount; ++CascadeIndex)
				{
					const float SplitRatio = static_cast<float>(CascadeIndex + 1) / static_cast<float>(DirectionalCascadeCount);

					const float LinearSplit = NearClip + (FarClip - NearClip) * SplitRatio;

					const float LogSplit = NearClip * std::pow(FarClip / NearClip, SplitRatio);

					DirectionalCascadeSplits[CascadeIndex] = View.bIsPerspective ? CascadeSplitLambda * LogSplit + (1.0f - CascadeSplitLambda) * LinearSplit : LinearSplit;
				}
			}

			LightShadowOffsets[FSceneRenderData::DirectionalLightSlotIndex] = ShadowMapOffset;

			// 카메라의 방향 벡터를 구한다.
			// 엔진의 전방 축은 +X다.
			const FQuat CameraRotation = View.ViewRotation.Quaternion();

			const FVector CameraForward = View.ViewForward.GetSafeNormal();
			const FVector CameraRight = CameraRotation.GetRightVector().GetSafeNormal();
			const FVector CameraUp = CameraRotation.GetUpVector().GetSafeNormal();

			assert(View.NearClip > 0.0f);
			assert(View.FarClip > View.NearClip);
			assert(View.AspectRatio > 0.0f);

			const float HalfFOV =
				FMath::DegreesToRadians(View.FieldOfView) * 0.5f;

			for (int32 LightIndexOffset = 0; LightIndexOffset < GetDirectionalLightCount(); ++LightIndexOffset)
			{
				const int32 LightIndex = GetDirectionalLightOffset() + LightIndexOffset;

				const FLightRenderParameters& DirectionalLight = VisibleLights[LightIndex];

				const FVector LightDirection = DirectionalLight.Direction.GetSafeNormal();

				assert(LightDirection.SizeSquared() > 0.5f);

				// 4개의 Cascade를 각각 생성한다.
				for (uint32 CascadeIndex = 0; CascadeIndex < DirectionalCascadeCount; ++CascadeIndex)
				{
					const float CascadeNearDepth = CascadeIndex == 0 ? View.NearClip : DirectionalCascadeSplits[CascadeIndex - 1];

					const float CascadeFarDepth = DirectionalCascadeSplits[CascadeIndex];

					// 현재 Cascade에 해당하는 카메라 절두체의 8개 꼭짓점.
					FVector FrustumCorners[8];
					uint32 CornerIndex = 0;

					const float Depths[2] = { CascadeNearDepth, CascadeFarDepth };

					for (uint32 DepthIndex = 0; DepthIndex < 2; ++DepthIndex)
					{
						const float Depth = Depths[DepthIndex];

						float HalfWidth;
						float HalfHeight;

						if (View.bIsPerspective)
						{
							HalfHeight = std::tan(HalfFOV) * Depth;
							HalfWidth = HalfHeight * View.AspectRatio;
						}
						else
						{
							// 직교 카메라는 깊이가 달라져도 화면 영역이 같다.
							HalfWidth = View.OrthoWidth * 0.5f;
							HalfHeight = HalfWidth / View.AspectRatio;
						}

						const FVector SliceCenter = View.ViewLocation + CameraForward * Depth;

						for (int32 YSign = -1; YSign <= 1; YSign += 2)
						{
							for (int32 XSign = -1; XSign <= 1; XSign += 2)
							{
								FrustumCorners[CornerIndex++] = SliceCenter + CameraRight * (HalfWidth * static_cast<float>(XSign)) + CameraUp * (HalfHeight * static_cast<float>(YSign));
							}
						}
					}

					// 8개 꼭짓점의 평균으로 Cascade 중앙을 구한다.
					FVector CascadeCenter(0.0f, 0.0f, 0.0f);

					for (const FVector& Corner : FrustumCorners)
					{
						CascadeCenter += Corner;
					}

					CascadeCenter /= 8.0f;

					// 중앙을 기준으로 8개 꼭짓점을 모두 포함하는 구의 반지름.
					float CascadeRadius = 0.0f;

					for (const FVector& Corner : FrustumCorners)
					{
						CascadeRadius = FMath::Max(CascadeRadius, (Corner - CascadeCenter).Size());
					}

					// 투영 행렬이 퇴화하는 것을 방지한다.
					CascadeRadius = FMath::Max(CascadeRadius, 1.0f);

					// 광원 카메라를 Cascade 중앙보다 빛의 반대 방향으로 이동한다.
					// 여유 공간을 두어 절두체 꼭짓점이 Near/Far 범위에서 잘리지 않게 한다.
					constexpr float DepthPadding = 100.0f;

					const FVector LightViewLocation = CascadeCenter - LightDirection * (CascadeRadius + DepthPadding);

					const FMatrix LightView = LookAt(LightViewLocation, CascadeCenter, UpVector);

					// 정사영 영역은 정사각형으로 잡아 모든 꼭짓점을 포함한다.
					const float ShadowWidth = CascadeRadius * 2.0f;

					const float ShadowNear = 0.1f;
					const float ShadowFar = 2.0f * (CascadeRadius + DepthPadding);

					const FMatrix LightProjection = Ortho(ShadowWidth, 1.0f, ShadowNear, ShadowFar);

					FLightShadowRenderData Shadow{};

					Shadow.ViewProjection = LightView * LightProjection;

					// 셰이더에서 현재 Cascade의 선택 기준으로 사용한다.
					Shadow.CascadeFarDepth = CascadeFarDepth;

					const uint32 ShadowIndex = LightShadows.Add(Shadow);

					AreaInfos.Add({DirectionalLightShadowMapSize, ShadowIndex});

					TotalAreaSize += DirectionalLightShadowMapSize * DirectionalLightShadowMapSize;

					MaxAreaSize = FMath::Max(MaxAreaSize, DirectionalLightShadowMapSize);

					++ShadowMapOffset;
				}
			}

			std::sort(AreaInfos.GetData(), AreaInfos.GetData() + AreaInfos.Num(), [](const FAreaInfo& A, const FAreaInfo& B) { return A.Size > B.Size; });

			// TODO: 패킹 알고리즘 필요
			uint32 AtlasCursorX = 0;
			uint32 AtlasCursorY = 0;
			uint32 MaxHeightInLine = 0;
			for (int32 i = 0; i < AreaInfos.Num(); i++)
			{
				FAreaInfo& AreaInfo = AreaInfos[i];

				if (AtlasCursorX + AreaInfo.Size > ShadowMapSize)
				{
					AtlasCursorY += MaxHeightInLine;
					AtlasCursorX = 0;
					MaxHeightInLine = 0;

					if (AtlasCursorY + AreaInfo.Size > ShadowMapSize)
					{
						AtlasNeedCount++;
						AtlasCursorY = 0;
						MaxHeightInLine = 0;
					}
				}

				FLightShadowRenderData& RenderData = LightShadows[AreaInfo.Index];

				RenderData.AtlasIndex = AtlasNeedCount - 1;
				RenderData.AtlasRect.X = AtlasCursorX;
				RenderData.AtlasRect.Y = AtlasCursorY;
				RenderData.AtlasRect.Width = AreaInfo.Size;
				RenderData.AtlasRect.Height = AreaInfo.Size;

				AtlasCursorX += AreaInfo.Size;

				MaxHeightInLine = std::max(MaxHeightInLine, AreaInfo.Size);
			}
		}

		// NOTE: 라이트 데이터를 GPU에 올림.
		bLightUpaloadSucceeded = ViewLightResources.Init();
		if (bLightUpaloadSucceeded)
		{
			bLightUpaloadSucceeded &= ViewLightResources.UploadViewLights(VisibleLights);
			ViewLightResources.CreateShadowAtlasIfNeed(ShadowMapSize, ShadowMapSize, AtlasNeedCount);
			bLightUpaloadSucceeded &= ViewLightResources.UploadLightShadows(LightShadows);
		}
	}

	inline bool HasDirectionalLight() { return LightParameterCounts[DirectionalLightSlotIndex]; }
	inline uint32 GetDirectionalLightOffset() { return LightParameterOffsets[DirectionalLightSlotIndex]; }
	inline uint32 GetDirectionalLightCount() { return LightParameterCounts[DirectionalLightSlotIndex]; }
	inline uint32 GetDirectionalLightInfoOffset() { return LightShadowOffsets[DirectionalLightSlotIndex]; }
	
	inline bool HasPointLights() { return LightParameterCounts[PointLightSlotIndex]; }
	inline uint32 GetPointLightOffset() { return LightParameterOffsets[PointLightSlotIndex]; }
	inline uint32 GetPointLightCount() { return LightParameterCounts[PointLightSlotIndex]; }
	inline uint32 GetPointLightShadowOffset() { return LightShadowOffsets[PointLightSlotIndex]; }

	inline bool HasSpotLights() { return LightParameterCounts[SpotLightSlotIndex]; }
	inline uint32 GetSpotLightOffset() { return LightParameterOffsets[SpotLightSlotIndex]; }
	inline uint32 GetSpotLightCount() { return LightParameterCounts[SpotLightSlotIndex]; }
	inline uint32 GetSpotLightInfoOffset() { return LightShadowOffsets[SpotLightSlotIndex]; }

	inline bool HasRectLights() { return LightParameterCounts[RectLightSlotIndex]; }
	inline bool HasLights() { return HasDirectionalLight() || HasPointLights() || HasSpotLights() || HasRectLights(); }
};
