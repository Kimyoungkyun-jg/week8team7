#pragma once

#include "Render/RenderPacket.h"
#include "Render/ObjectConstants.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/LightSceneProxy.h"
#include "Render/LightRenderData.h"
#include "Render/SceneLightResources.h"
#include "Math/Matrix.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>
#include "Math/QuadTree.h"

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	uint32 DirectionalCount = 0;
	uint32 PointCount = 0;
	uint32 SpotCount = 0;
	uint32 RectCount = 0;

	float AtlasCapacity = 0.0f;
	float ShadowTiles = 0.0f;

	// 이번 프레임에 캐시를 재사용해 렌더링을 생략한 Shadow View 수
	uint32 ShadowViewsReused = 0;

	// 이번 프레임에 실제로 다시 렌더링한 Shadow View 수
	uint32 ShadowViewsRedrawn = 0;

	void Reset() { *this = FRenderStats(); }
};

struct FShadowMapCacheKey
{
	const ULightComponent* Light = nullptr;

	uint32 LightType = 0;
	uint32 ViewIndex = 0;

	bool Matches(const FShadowMapCacheKey& Other) const
	{
		return Light == Other.Light && LightType == Other.LightType && ViewIndex == Other.ViewIndex;
	}
};

struct FShadowMapCacheEntry
{
	FShadowMapCacheKey Key;

	uint32 RequestedSize = 0;
	uint32 AtlasIndex = UINT32_MAX;
	FRect AtlasRect{};

	FMatrix CachedViewProjection{};
	float CachedNear = 0.0f;
	float CachedFar = 0.0f;

	uint64 CachedCasterRevision = 0;
	uint64 CachedCasterSignature = 0;

	bool bHasValidDepth = false;
	bool bSeenThisFrame = false;
};

// 뷰 하나의 데이터를 프레임 사이에도 보관한다. SceneRenderer는 현재 프레임 동안 이를 빌려 쓴다.
struct FSceneRenderData
{
	static constexpr int32 PointLightSlotIndex = 0;
	static constexpr int32 SpotLightSlotIndex = 1;
	static constexpr int32 DirectionalLightSlotIndex = 2;
	static constexpr int32 RectLightSlotIndex = 3;
	
	static constexpr uint32 ShadowMapSize = 4096;
	static constexpr uint32 MaxCascadeShadowCount = 4;
	static constexpr float MaxCascadeDistance = 1000.f;

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
	TArray<FPrimitiveSceneProxy*> ShadowCasterProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;
	TArray<FGatherChunk> GatherChunks;

	TArray<FPerObjectConstants> ObjectConstants;
	FSceneLightResources ViewLightResources;

	FAmbientLightRenderData AmbientLight{};
	FSkyLightRenderData SkyLight{};
	bool bHasSkyLight = false;

	TArray<FLightRenderParameters> VisibleLights; // 모든 종류의 광원. 업로드할 때 종류별 구간으로 정렬한다
	TArray<const ULightComponent*> VisibleLightOwners;
	TArray<FShadowMapCacheKey> CurrentShadowKeys;
	TArray<uint8> ShadowNeedsRender;
	TArray<FShadowMapCacheEntry> ShadowMapCache;
	TArray<FQuadTree> AtlasTrees;
	uint64 CurrentCasterRevision = 0;
	TArray<uint64> CurrentShadowCasterSignatures;

	uint32 LightParameterOffsets[4]; // 라이트 리스트 내 구간의 offset
	uint32 LightParameterCounts[4]; // 라이트 리스트 내 구간의 count

	uint32 DirectionalLightShadowMapSize = 4096;
	uint32 PointLightShadowMapSize = 512;
	uint32 SpotLightShadowMapSize = 512;

	TArray<FLightShadowRenderData> LightShadows;
	uint32 LightShadowOffsets[4]; // 라이트 그림자 리스트 내 구간의 offset

	float CascadeShadowDistances[MaxCascadeShadowCount]{};
	uint32 CascadeShadowCount = 0;
	// CPU-only preview state for this view. Never uploaded with the light parameters.
	const ULightComponent* PreviewLight = nullptr;
	int32 PreviewLightIndex = INDEX_NONE;
	FLightShadowRange PreviewShadowRange{};

	int32 FindShadowCacheIndex(const FShadowMapCacheKey& Key) const
	{
		for (int32 i = 0; i < ShadowMapCache.Num(); ++i)
		{
			if (ShadowMapCache[i].Key.Matches(Key)) return i;
		}
		return INDEX_NONE;
	}

	void ReleaseShadowAllocation(FShadowMapCacheEntry& Cache)
	{
		if (Cache.AtlasIndex != UINT32_MAX)
		{
			assert(Cache.AtlasIndex < static_cast<uint32>(AtlasTrees.Num()));
			const bool bReleased = Cache.AtlasIndex < static_cast<uint32>(AtlasTrees.Num()) && AtlasTrees[Cache.AtlasIndex].ReleaseSquare(Cache.AtlasRect);
			assert(bReleased && "Shadow cache points to an unallocated Atlas rectangle");
		}
		Cache.AtlasIndex = UINT32_MAX;
		Cache.AtlasRect = {};
		Cache.bHasValidDepth = false;
	}

	int32 FindOrCreateShadowCache(const FShadowMapCacheKey& Key, uint32 RequestedSize)
	{
		int32 Index = FindShadowCacheIndex(Key);
		if (Index == INDEX_NONE)
		{
			Index = ShadowMapCache.Num();
			FShadowMapCacheEntry& NewEntry = ShadowMapCache.AddDefaulted_GetRef();
			NewEntry.Key = Key;
			NewEntry.RequestedSize = RequestedSize;
			NewEntry.bSeenThisFrame = true;
			return Index;
		}

		FShadowMapCacheEntry& Entry = ShadowMapCache[Index];
		Entry.bSeenThisFrame = true;
		if (Entry.RequestedSize != RequestedSize)
		{
			ReleaseShadowAllocation(Entry);
			Entry.RequestedSize = RequestedSize;
		}
		return Index;
	}

	void BeginShadowCacheFrame() 
	{
		for (FShadowMapCacheEntry& Entry : ShadowMapCache) Entry.bSeenThisFrame = false;
	}

	void PruneShadowCache()
	{
		for (int32 i = ShadowMapCache.Num() - 1; i >= 0;--i)
		{
			if (!ShadowMapCache[i].bSeenThisFrame)
			{
				ReleaseShadowAllocation(ShadowMapCache[i]);
				ShadowMapCache.RemoveAtSwap(i);
			}
		}
	}

	static bool IsSameViewProjection(const FMatrix& A, const FMatrix& B)
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				if (A.M[Row][Column] != B.M[Row][Column]) return false;
			}
		}
		return true;
	}

	uint64 BuildLocalLightCasterSignature(const FVector& LightPosition, float LightRadius) const
	{
		TArray<const FPrimitiveSceneProxy*> RelevantCasters;
		const float RadiusSquared = LightRadius * LightRadius;
		for (const FPrimitiveSceneProxy* Proxy : ShadowCasterProxies)
		{
			if (!Proxy || !Proxy->bCastsShadow || !Proxy->GetMesh() || !Proxy->GetRenderState())
			{
				continue;
			}
			const FAABB& Bounds = Proxy->GetBounds();
			const float ClosestX = std::clamp(LightPosition.X, Bounds.Center.X - Bounds.Extent.X, Bounds.Center.X + Bounds.Extent.X);
			const float ClosestY = std::clamp(LightPosition.Y, Bounds.Center.Y - Bounds.Extent.Y, Bounds.Center.Y + Bounds.Extent.Y);
			const float ClosestZ = std::clamp(LightPosition.Z, Bounds.Center.Z - Bounds.Extent.Z, Bounds.Center.Z + Bounds.Extent.Z);
			const float DX = LightPosition.X - ClosestX;
			const float DY = LightPosition.Y - ClosestY;
			const float DZ = LightPosition.Z - ClosestZ;
			const float DistanceSquared = DX * DX + DY * DY + DZ * DZ;
			if (DistanceSquared > RadiusSquared)
			{
				continue;
			}
			RelevantCasters.Add(Proxy);
		}

		if (RelevantCasters.Num() > 1)
		{
			std::sort(RelevantCasters.GetData(), RelevantCasters.GetData() + RelevantCasters.Num(), std::less<const FPrimitiveSceneProxy*>{});
		}
		uint64 Hash = 14695981039346656037ull;

		for (const FPrimitiveSceneProxy* Proxy : RelevantCasters)
		{
			Hash ^= static_cast<uint64>(std::hash<const FPrimitiveSceneProxy*>{}(Proxy));
			Hash *= 1099511628211ull;

			Hash ^= Proxy->GetShadowCasterRevision();
			Hash *= 1099511628211ull;
		}

		return Hash;
	}

	void PrepareShadowRenderFlags(bool bAtlasRecreated)
	{
		RenderStats.ShadowViewsReused = 0;
		RenderStats.ShadowViewsRedrawn = 0;
		if (bAtlasRecreated)
		{
			for (FShadowMapCacheEntry& Entry : ShadowMapCache) Entry.bHasValidDepth = false;
		}

		ShadowNeedsRender.SetNum(LightShadows.Num());
		assert(CurrentShadowKeys.Num() == LightShadows.Num());
		for (int32 i = 0; i < LightShadows.Num(); ++i)
		{
			const int32 CacheIndex = FindShadowCacheIndex(CurrentShadowKeys[i]);
			if (CacheIndex == INDEX_NONE)
			{
				ShadowNeedsRender[i] = 1;
				assert(false && "Missing Shadow Atlas cache entry");
				continue;
			}
			const FShadowMapCacheEntry& Cache = ShadowMapCache[CacheIndex];
			const FLightShadowRenderData& Shadow = LightShadows[i];
			assert(CurrentShadowCasterSignatures.Num() == LightShadows.Num());

			const bool bRangeLimitedLight = Cache.Key.LightType == static_cast<uint32>(ELightType::Point) || Cache.Key.LightType == static_cast<uint32>(ELightType::Spot);

			const bool bCasterChanged = bRangeLimitedLight ? Cache.CachedCasterSignature != CurrentShadowCasterSignatures[i] : Cache.CachedCasterRevision != CurrentCasterRevision;

			const bool bDirty = !Cache.bHasValidDepth || !IsSameViewProjection( Cache.CachedViewProjection, Shadow.ViewProjection) || Cache.CachedNear != Shadow.Near || Cache.CachedFar != Shadow.Far || bCasterChanged;

			ShadowNeedsRender[i] = bDirty ? 1u : 0u;

			if (!bDirty)
			{
				++RenderStats.ShadowViewsReused;
			}
		}
	}

	void MarkShadowRendered(int32 ShadowIndex)
	{
		if (ShadowIndex < 0 || ShadowIndex >= LightShadows.Num() || ShadowIndex >= CurrentShadowKeys.Num())
		{
			assert(false && "Invalid rendered Shadow View index");
			return;
		}

		const int32 CacheIndex = FindShadowCacheIndex(CurrentShadowKeys[ShadowIndex]);
		if (CacheIndex == INDEX_NONE)
		{
			assert(false && "Rendered Shadow View has no cache entry");
			return;
		}

		FShadowMapCacheEntry& Cache = ShadowMapCache[CacheIndex];
		const FLightShadowRenderData& Shadow = LightShadows[ShadowIndex];
		Cache.CachedViewProjection = Shadow.ViewProjection;
		Cache.CachedNear = Shadow.Near;
		Cache.CachedFar = Shadow.Far;
		Cache.CachedCasterRevision = CurrentCasterRevision;
		assert(ShadowIndex >= 0 && ShadowIndex < CurrentShadowCasterSignatures.Num());

		Cache.CachedCasterSignature = CurrentShadowCasterSignatures[ShadowIndex];
		Cache.bHasValidDepth = true;
	}

	bool GetShadowPreview(const ULightComponent* Light, FTexture2DArray*& OutAtlas,  // Shadow range를 바탕으로 lightshadow 인덱스에서 선택한 조명이 몇번째 index에서 viewcount가 몇개인지 알 수 있음
		TArray<FLightShadowRenderData>& OutShadows) const
	{
		OutAtlas = nullptr;
		OutShadows.Reset();
		const uint32 Start = PreviewShadowRange.StartIndex;
		const uint32 Count = PreviewShadowRange.ViewCount;
		if (!Light || Light != PreviewLight || !Count || Start > uint32(LightShadows.Num()) ||
			Count > uint32(LightShadows.Num()) - Start)
			return false;
		FTexture2DArray* Atlas = ViewLightResources.GetShadowMapAtlas();
		if (!Atlas || !Atlas->GetSRV()) return false;
		for (uint32 i = 0; i < Count; ++i)
		{
			const auto& Shadow = LightShadows[Start + i];
			if (Shadow.AtlasIndex >= Atlas->GetArraySize() || Shadow.AtlasRect.Width <= 0 || Shadow.AtlasRect.Height <= 0)
			{
				OutShadows.Reset();
				return false;
			}
			OutShadows.Add(Shadow);
		}
		OutAtlas = Atlas;
		return true;
	}

	void ResetScene()
	{
		ResetFrame();
		ShadowMapCache.Reset();
		AtlasTrees.Reset();
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
		ShadowCasterProxies.Reset();
		LODInputs.Reset();
		SelectedLODs.Reset();
		ObjectConstants.Reset();
		AmbientLight = {};
		SkyLight = {};
		bHasSkyLight = false;
		VisibleLights.Reset();
		VisibleLightOwners.Reset();
		CurrentShadowKeys.Reset();
		ShadowNeedsRender.Reset();
		CurrentShadowCasterSignatures.Reset();
		LightShadows.Reset();
		PreviewLightIndex = INDEX_NONE;
		PreviewShadowRange = {};
		CascadeShadowCount = 0;
		std::fill(std::begin(CascadeShadowDistances), std::end(CascadeShadowDistances), 0.0f);
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

	void Commit(const FFrustumCorners& CameraFrustumCorners, float CameraNear, float CameraFar, uint32 ExpectCascadeCount, bool& bLightUpaloadSucceeded)
	{
		RenderQueue.Sort();
		BeginShadowCacheFrame();
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

			// Sort indices so the selected light's identity survives sorting, including identical lights.
			TArray<int32> LightOrder;
			LightOrder.SetNum(VisibleLights.Num());
			for (int32 i = 0; i < VisibleLights.Num(); ++i) LightOrder[i] = i;
			std::sort(LightOrder.begin(), LightOrder.end(), [&](int32 A, int32 B) {
				const uint32 ASlot = TypeToSlot[VisibleLights[A].Type];
				const uint32 BSlot = TypeToSlot[VisibleLights[B].Type];
				return ASlot < BSlot;
			});
			TArray<FLightRenderParameters> SortedLights;
			TArray<const ULightComponent*> SortedLightOwners;
			int32 SortedPreviewIndex = INDEX_NONE;
			for (int32 Index : LightOrder)
			{
				if (Index == PreviewLightIndex) SortedPreviewIndex = SortedLights.Num();
				SortedLights.Add(VisibleLights[Index]);
				SortedLightOwners.Add(VisibleLightOwners[Index]);
			}
			VisibleLights = std::move(SortedLights);
			VisibleLightOwners = std::move(SortedLightOwners);
			PreviewLightIndex = SortedPreviewIndex;

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
				FShadowMapCacheKey Key;
			};

			TArray<FAreaInfo> AreaInfos;

			uint32 ShadowOffset = 0;

			LightShadowOffsets[FSceneRenderData::PointLightSlotIndex] = ShadowOffset;
			for (int32 i = 0; i < GetPointLightCount(); i++)
			{
				int32 LightIndex = GetPointLightOffset() + i;
				if (LightIndex == PreviewLightIndex) PreviewShadowRange = { uint32(LightShadows.Num()), 6 };

				const FLightRenderParameters& PointLight = VisibleLights[LightIndex];
				const uint64 PointCasterSignature = BuildLocalLightCasterSignature(PointLight.Position, PointLight.Radius);
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
					Shadow.Near = 0.1f;
					Shadow.Far = PointLight.Radius;

					const FShadowMapCacheKey Key
					{
						VisibleLightOwners[LightIndex],
						PointLight.Type,
						static_cast<uint32>(j)
					};

					uint32 Index = LightShadows.Add(Shadow);

					CurrentShadowKeys.Add(Key);
					CurrentShadowCasterSignatures.Add(PointCasterSignature);
					AreaInfos.Add({ PointLightShadowMapSize, Index, Key });

					ShadowOffset++;
				}
			}

			LightShadowOffsets[FSceneRenderData::SpotLightSlotIndex] = ShadowOffset;
			for (int32 i = 0; i < GetSpotLightCount(); i++)
			{
				int32 LightIndex = GetSpotLightOffset() + i;
				if (LightIndex == PreviewLightIndex) PreviewShadowRange = { uint32(LightShadows.Num()), 1 };

				const FLightRenderParameters& SpotLight = VisibleLights[LightIndex];
				const uint64 SpotCasterSignature = BuildLocalLightCasterSignature(SpotLight.Position, SpotLight.Radius);
				FMatrix ViewMatrix = LookAt(SpotLight.Position, SpotLight.Position + SpotLight.Direction, UpVector);
				FMatrix ProjectionMatrix = Perspective(std::acos(SpotLight.OuterConeCos) * 2.0f, 1.0, 0.1f, SpotLight.Radius);

				FLightShadowRenderData Shadow{};
				Shadow.ViewProjection = ViewMatrix * ProjectionMatrix;
				Shadow.Near = 0.1f;
				Shadow.Far = SpotLight.Radius;
				const FShadowMapCacheKey Key
				{
					VisibleLightOwners[LightIndex],
					SpotLight.Type,
					0u
				};
				uint32 Index = LightShadows.Add(Shadow);
				CurrentShadowKeys.Add(Key);
				CurrentShadowCasterSignatures.Add(SpotCasterSignature);
				AreaInfos.Add({ SpotLightShadowMapSize, Index , Key});

				ShadowOffset++;
			}

			float CascadeShadowFar = FMath::Min(CameraFar, MaxCascadeDistance);
			float LogRatio = 0.8;

			CascadeShadowCount = FMath::Clamp(ExpectCascadeCount, 1u, MaxCascadeShadowCount);

			LightShadowOffsets[FSceneRenderData::DirectionalLightSlotIndex] = ShadowOffset;
			for (int32 i = 0; i < GetDirectionalLightCount(); i++)
			{
				int32 LightIndex = GetDirectionalLightOffset() + i;
				if (LightIndex == PreviewLightIndex) PreviewShadowRange = { uint32(LightShadows.Num()), CascadeShadowCount };

				const FLightRenderParameters& DirectionalLight = VisibleLights[LightIndex];
				FMatrix ViewMatrix = LookAt(FVector(0.f), DirectionalLight.Direction, UpVector);

				FVector ToTL = CameraFrustumCorners.FTL() - CameraFrustumCorners.NTL();
				FVector ToTR = CameraFrustumCorners.FTR() - CameraFrustumCorners.NTR();
				FVector ToBL = CameraFrustumCorners.FBL() - CameraFrustumCorners.NBL();
				FVector ToBR = CameraFrustumCorners.FBR() - CameraFrustumCorners.NBR();

				FVector PrevTL = CameraFrustumCorners.NTL();
				FVector PrevTR = CameraFrustumCorners.NTR();
				FVector PrevBL = CameraFrustumCorners.NBL();
				FVector PrevBR = CameraFrustumCorners.NBR();

				uint32 CascadeShadowMapSize = DirectionalLightShadowMapSize;
				for (int32 j = 0; j < CascadeShadowCount; j++)
				{
					float Linear = CameraNear + (CascadeShadowFar - CameraNear) * ((j + 1) / (float)CascadeShadowCount);
					float Log = CameraNear * std::pow(CascadeShadowFar / CameraNear, (j + 1) / (float)CascadeShadowCount);
					float Dist = (1.f - LogRatio) * Linear + LogRatio * Log;

					float T = (Dist - CameraNear) / (CameraFar - CameraNear);

					FVector LT = CameraFrustumCorners.NTL() + ToTL * T;
					FVector RT = CameraFrustumCorners.NTR() + ToTR * T;
					FVector LB = CameraFrustumCorners.NBL() + ToBL * T;
					FVector RB = CameraFrustumCorners.NBR() + ToBR * T;

					FVector* WorldCorners[] = {
						&PrevTL,
						&PrevTR,
						&PrevBL,
						&PrevBR,
						&LT,
						&RT,
						&LB,
						&RB
					};

					FVector CascadeCenter(0.f);
					for (int32 k = 0; k < 8; k++)
						CascadeCenter += *WorldCorners[k];
					CascadeCenter *= 1.f / 8.f;

					float RadiusSquared = 0.f;
					float MinLightX = FLT_MAX;
					float MaxLightX = -FLT_MAX;
					for (int32 k = 0; k < 8; k++)
					{
						RadiusSquared = std::max(RadiusSquared, (*WorldCorners[k] - CascadeCenter).SizeSquared());
						FVector4 InCascadeLightSpace = FVector4(*WorldCorners[k], 1.0f) * ViewMatrix;
						MinLightX = std::min(MinLightX, InCascadeLightSpace.X);
						MaxLightX = std::max(MaxLightX, InCascadeLightSpace.X);
					}

					// 회전해도 투영 크기가 변하지 않도록 cascade를 구로 감싼다. 부동 소수점 오류 방지 위해 격자점에 snap
					const float Radius = std::ceil(std::sqrt(RadiusSquared) * 16.f) / 16.f;
					const float Extent = Radius * CascadeShadowMapSize / (CascadeShadowMapSize - 1.f); // 패딩 공간 주기 위해 n / n-1 배 해주기
					const float TexelSize = 2.f * Extent / CascadeShadowMapSize;

					FVector4 CenterInLight = FVector4(CascadeCenter, 1.f) * ViewMatrix;

					const float CenterY = std::round(CenterInLight.Y / TexelSize) * TexelSize; // 중심을 가까운 격자점에 스냅함
					const float CenterZ = std::round(CenterInLight.Z / TexelSize) * TexelSize;
					float NearX = MinLightX;

					for (const FPrimitiveSceneProxy* Caster : ShadowCasterProxies)
					{
						const FAABB& Bounds = Caster->GetBounds();
						const FVector4 LightCenter = FVector4(Bounds.Center, 1.f) * ViewMatrix;
						const FVector LightExtent( // lightview 공간에서의 물체의 새로운 aabb 박스
							std::abs(ViewMatrix.M[0][0]) * Bounds.Extent.X + std::abs(ViewMatrix.M[1][0]) * Bounds.Extent.Y + std::abs(ViewMatrix.M[2][0]) * Bounds.Extent.Z,
							std::abs(ViewMatrix.M[0][1]) * Bounds.Extent.X + std::abs(ViewMatrix.M[1][1]) * Bounds.Extent.Y + std::abs(ViewMatrix.M[2][1]) * Bounds.Extent.Z,
							std::abs(ViewMatrix.M[0][2]) * Bounds.Extent.X + std::abs(ViewMatrix.M[1][2]) * Bounds.Extent.Y + std::abs(ViewMatrix.M[2][2]) * Bounds.Extent.Z);

						if (LightCenter.Y + LightExtent.Y < CenterY - Extent || LightCenter.Y - LightExtent.Y > CenterY + Extent ||
							LightCenter.Z + LightExtent.Z < CenterZ - Extent || LightCenter.Z - LightExtent.Z > CenterZ + Extent ||
							LightCenter.X - LightExtent.X > MaxLightX)
							continue;

						NearX = std::min(NearX, LightCenter.X - LightExtent.X);
					}
					NearX -= 1.f;
					FMatrix CascadeProjectionMatrix = Ortho(CenterY - Extent, CenterY + Extent, CenterZ + Extent, CenterZ - Extent, NearX, MaxLightX);

					FLightShadowRenderData Shadow{};
					Shadow.ViewProjection = ViewMatrix * CascadeProjectionMatrix;
					Shadow.Near = NearX;
					Shadow.Far = MaxLightX;
					const FShadowMapCacheKey Key
					{
						VisibleLightOwners[LightIndex],
						DirectionalLight.Type,
						static_cast<uint32>(j)
					};
					uint32 Index = LightShadows.Add(Shadow);
					CurrentShadowKeys.Add(Key);
					CurrentShadowCasterSignatures.Add(0);
					AreaInfos.Add({ DirectionalLightShadowMapSize, Index , Key });

					ShadowOffset++;

					PrevTL = LT;
					PrevTR = RT;
					PrevBL = LB;
					PrevBR = RB;

					CascadeShadowDistances[j] = Dist;
					// CascadeShadowMapSize /= 2;
				}
			}

			for (const FAreaInfo& AreaInfo : AreaInfos)
			{
				FindOrCreateShadowCache(AreaInfo.Key, AreaInfo.Size);
			}
			PruneShadowCache();

			if (AtlasTrees.IsEmpty())
			{
				AtlasTrees.Add(FQuadTree(ShadowMapSize));
			}
			for (uint32 Size = ShadowMapSize; Size > 0; Size >>= 1)
			{
				for (const FAreaInfo& AreaInfo : AreaInfos)
				{
					if (AreaInfo.Size != Size) continue;

					const int32 CacheIndex = FindShadowCacheIndex(AreaInfo.Key);
					assert(CacheIndex != INDEX_NONE);
					if (CacheIndex == INDEX_NONE) continue;

					FShadowMapCacheEntry& Cache = ShadowMapCache[CacheIndex];
					if (Cache.AtlasIndex != UINT32_MAX) continue;

					FRect PlacedRect{};
					bool bPlaced = false;
					uint32 PlacedAtlasIndex = 0;
					for (int32 Page = 0; Page < AtlasTrees.Num(); ++Page)
					{
						if (AtlasTrees[Page].InsertSquare(static_cast<uint16>(AreaInfo.Size), PlacedRect))
						{
							PlacedAtlasIndex = static_cast<uint32>(Page);
							bPlaced = true;
							break;
						}
					}

					if (!bPlaced)
					{
						AtlasTrees.Add(FQuadTree(ShadowMapSize));
						PlacedAtlasIndex = static_cast<uint32>(AtlasTrees.Num() - 1);
						bPlaced = AtlasTrees.Last().InsertSquare(static_cast<uint16>(AreaInfo.Size), PlacedRect);
					}
					if (!bPlaced)
					{
						bLightUpaloadSucceeded = false;
						assert(false && "Could not allocate a Shadow Atlas rectangle");
						return;
					}
					Cache.AtlasIndex = PlacedAtlasIndex;
					Cache.AtlasRect = PlacedRect;
					Cache.bHasValidDepth = false;
				}
				if (Size == 1) break;
			}

			for (const FAreaInfo& AreaInfo : AreaInfos)
			{
				const int32 CacheIndex = FindShadowCacheIndex(AreaInfo.Key);
				assert(CacheIndex != INDEX_NONE);
				if (CacheIndex == INDEX_NONE) continue;
				const FShadowMapCacheEntry& Cache = ShadowMapCache[CacheIndex];
				FLightShadowRenderData& Shadow = LightShadows[AreaInfo.Index];
				Shadow.AtlasIndex = Cache.AtlasIndex;
				Shadow.AtlasRect = Cache.AtlasRect;
			}
			AtlasNeedCount = std::max(1u, static_cast<uint32>(AtlasTrees.Num()));
		}
		else
		{
			PruneShadowCache();
			AtlasNeedCount = std::max(1u, static_cast<uint32>(AtlasTrees.Num()));
		}
		// NOTE: 라이트 데이터를 GPU에 올림.
		bLightUpaloadSucceeded = ViewLightResources.Init();
		if (bLightUpaloadSucceeded)
		{
			bLightUpaloadSucceeded &= ViewLightResources.UploadViewLights(VisibleLights);
			bool bAtlasRecreated = false;
			const bool bAtlasReady = ViewLightResources.CreateShadowAtlasIfNeed(ShadowMapSize, ShadowMapSize, AtlasNeedCount, bAtlasRecreated);
			bLightUpaloadSucceeded &= bAtlasReady;
			if (bAtlasReady)
			{
				PrepareShadowRenderFlags(bAtlasRecreated);
				bLightUpaloadSucceeded &= ViewLightResources.UploadLightShadows(LightShadows);
			}
		}
		RenderStats.DirectionalCount = GetDirectionalLightCount();
		RenderStats.PointCount = GetPointLightCount();
		RenderStats.SpotCount = GetSpotLightCount();
		RenderStats.AtlasCapacity = ShadowMapSize * ShadowMapSize * AtlasNeedCount * 4;
		RenderStats.ShadowTiles = DirectionalLightShadowMapSize * DirectionalLightShadowMapSize * 4 * RenderStats.DirectionalCount;
		RenderStats.ShadowTiles += PointLightShadowMapSize * PointLightShadowMapSize * 6 * RenderStats.PointCount;
		RenderStats.ShadowTiles += SpotLightShadowMapSize * SpotLightShadowMapSize * RenderStats.SpotCount;
		RenderStats.ShadowTiles *= 4;
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
