#include "EnginePCH.h"
#include "Render/SceneLightResources.h"
#include "Render/RenderCommand.h"
#include "Render/MeshPassProcessor.h"
#include "Render/Renderer.h"
#include "Components/LightComponentBase.h"
#include "SceneRenderData.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	static_assert(offsetof(FDeferredLightConstants, ViewportSize) == 96);
	static_assert(offsetof(FDeferredLightConstants, Ambient) == 112);
	static_assert(offsetof(FDeferredLightConstants, LightCounts) == 128);
	static_assert(offsetof(FDeferredLightConstants, LightOffsets) == 144);
	static_assert(offsetof(FDeferredLightConstants, LightShadowOffsets) == 160);
	static_assert(offsetof(FDeferredLightConstants, SkyColorIntensity) == 176);
	static_assert(offsetof(FDeferredLightConstants, ShadowAtlasWidth) == 192);
	static_assert(offsetof(FDeferredLightConstants, ShadowAtlasHeight) == 196);
	static_assert(offsetof(FDeferredLightConstants, CascadeShadowCount) == 200);
	static_assert(offsetof(FDeferredLightConstants, PcfMode) == 204);
	static_assert(offsetof(FDeferredLightConstants, CascadeShadowDistances) == 208);
	static_assert(sizeof(FDeferredLightConstants) == 224);

	// 용량이 모자랄 때만 두 배로 키우고 Map 한 번으로 채운다.
	bool UploadStructured(TUniquePtr<FStructuredBuffer>& Buffer, const void* Data, uint32 Count, uint32 Stride)
	{
		if (!Count) return true;
		if (!Buffer || Buffer->GetCapacity() < Count)
		{
			const uint32 PreviousCapacity = Buffer ? Buffer->GetCapacity() : 0;
			const uint32 Maximum = UINT32_MAX / Stride;
			const uint32 Doubled = PreviousCapacity <= Maximum / 2
				? std::max(1u, PreviousCapacity * 2) : Maximum;
			auto NewBuffer = RenderCommand::CreateDynamicStructuredBuffer(std::max(Count, Doubled), Stride);
			if (!NewBuffer || !NewBuffer->GetBuffer() || !NewBuffer->GetSRV()) return false;
			Buffer = std::move(NewBuffer);
		}
		void* Destination = RenderCommand::MapWriteDiscard(Buffer.get());
		if (!Destination) return false;
		std::memcpy(Destination, Data, static_cast<size_t>(Count) * Stride);
		RenderCommand::Unmap(Buffer.get());
		return true;
	}
}

bool FSceneLightResources::Init()
{
	if (bInitialized) return true;

	ForwardLightCB = RenderCommand::CreateConstantBuffer(sizeof(FForwardLightingConstants));
	DeferredLightCB = RenderCommand::CreateConstantBuffer(sizeof(FDeferredLightConstants));

	if (!ForwardTileGrid.Init())
		HTR_LOG(Warning, "Forward tile culling compute shader unavailable");
	bInitialized = ForwardLightCB && DeferredLightCB;
	return bInitialized;
}

bool FSceneLightResources::CreateShadowAtlasIfNeed(uint32 ShadowAtlasMapWidth, uint32 ShadowAtlasMapHeight, uint32 NeedAtlasCount, bool& bOutRecreated)
{
	bOutRecreated = false;
	NeedAtlasCount = std::max(1u, NeedAtlasCount);
	if (!SceneLightShadowMapAtlas || ShadowAtlasMapWidth > SceneLightShadowMapAtlas->GetWidth() || ShadowAtlasMapHeight > SceneLightShadowMapAtlas->GetHeight() || NeedAtlasCount > SceneLightShadowMapAtlas->GetArraySize())
	{
		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = ShadowAtlasMapWidth;
		Desc.Height = ShadowAtlasMapHeight;
		Desc.MipLevels = 1;
		Desc.ArraySize = NeedAtlasCount;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		// No stencil component: required for rectangular DSV clears through ClearView.
		Desc.Format = DXGI_FORMAT_R32_TYPELESS;

		TUniquePtr<FTexture2DArray> NewAtlas = RenderCommand::CreateTexture2DArray(Desc);
		if (!NewAtlas || !NewAtlas->GetRawPtr() || !NewAtlas->GetSRV() || !NewAtlas->GetDSV(0))
		{
			HTR_LOG(Error, "Failed to create Shadow Atlas texture array");
			return false;
		}
		SceneLightShadowMapAtlas = std::move(NewAtlas);
		bOutRecreated = true;
	}
	return SceneLightShadowMapAtlas && SceneLightShadowMapAtlas->GetRawPtr() && SceneLightShadowMapAtlas->GetSRV() && SceneLightShadowMapAtlas->GetArraySize() >= NeedAtlasCount;
}

bool FSceneLightResources::UploadViewLights(const TArray<FLightRenderParameters>& Lights)
{
	return UploadStructured(SceneLightBuffer, Lights.GetData(), Lights.Num(), sizeof(FLightRenderParameters));
}

bool FSceneLightResources::UploadLightShadows(const TArray<FLightShadowRenderData>& Shadows)
{
	return UploadStructured(SceneLightShadowBuffer, Shadows.GetData(), Shadows.Num(), sizeof(FLightShadowRenderData));
}


