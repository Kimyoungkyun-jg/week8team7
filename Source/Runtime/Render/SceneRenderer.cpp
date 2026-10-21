#include "EnginePCH.h"
#include "Render/SceneRenderer.h"
#include "Render/Renderer.h"
#include "Render/SceneLightResources.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/SceneViewTargets.h"
#include "Render/SkyboxRenderer.h"
#include "Render/RenderResourceManager.h"
#include "Engine/World.h"
#include "Tasks/Tasks.h"
#include "Core/Stats/LightweightStats.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/SkyLightEnvironment.h"
#include "Engine/SceneManagement.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <utility>

DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);

namespace
{
	FRenderGraphRasterPassDesc MakeViewportPass(const std::string& Name, FRenderGraphTextureRef Color,
		FRenderGraphTextureRef DepthTarget = {},
		ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		FRenderGraphRasterPassDesc Pass{};
		Pass.Name = Name;
		Pass.ColorAttachments = { { Color, ERenderGraphLoadAction::Load } };
		if (DepthTarget)
		{
			Pass.DepthAttachment = { DepthTarget, DepthLoad, 1.0f, 0 };
		}
		return Pass;
	}

	bool AddOverlayPass(FRenderGraphBuilder& Graph, const char* Name,
		FRenderGraphTextureRef Color, FRenderGraphTextureRef Depth,
		const std::function<void()>& Draw,
		ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		return !Draw || Graph.AddRasterPass(MakeViewportPass(Name, Color, Depth, DepthLoad), Draw);
	}
}

// 생성자
FSceneRenderer::FSceneRenderer(UWorld* InWorld, const FSceneView& InView, FSceneRenderData& InRenderData)
	: World(InWorld), View(InView), RenderData(InRenderData)
{
	CachedViewContext = View.ToViewContext();
}

// 구체와 절두체 평면 교차 판별
bool FSceneRenderer::IntersectSphereFrustum(const FVector4& PositionRadius, const FPlane (&Planes)[6]) const
{
	const float Radius = PositionRadius.W;
	for (const FPlane& Plane : Planes)
	{
		const float Distance = Plane.Normal.X * PositionRadius.X +
			Plane.Normal.Y * PositionRadius.Y +
			Plane.Normal.Z * PositionRadius.Z + Plane.Distance;

		if (Distance < -Radius)
		{
			return false;
		}
	}
	return true;
}

// 가시성 검사 및 렌더 큐 수집
void FSceneRenderer::InitViews(FRenderer* Renderer, const FSceneRenderOptions& Options)
{
	bLightUploadSucceeded = false;
	if (!World)
	{
		RenderData.ResetFrame();
		return;
	}

	// 월드 프리미티브 수집 및 절두체 컬링
	GatherRenderPackets(World->GetScene(), RenderData, CachedViewContext, &View.Frustum, Renderer);
	RenderData.AmbientLight = World->GetScene().GetSkyAmbientLight(View.bGameView);

	// 모든 프록시가 같은 FLightRenderParameters를 채우고, 종류는 Type으로만 구분한다.
	const FScene& Scene = World->GetScene();
	for (const FDirectionalLightSceneProxy* Light : Scene.GetDirectionalLights())
	{
		if (!Light->IsEnabled() || !Light->IsShown(View.bGameView)) continue;
		FLightRenderParameters Parameters{};
		Light->GetLightShaderParameters(Parameters);
		const int32 Index = RenderData.VisibleLights.Add(Parameters);
		RenderData.VisibleLightOwners.Add(Light->GetLightComponent());
		if (Light->GetLightComponent() == RenderData.PreviewLight) RenderData.PreviewLightIndex = Index;
	}

	for (const FLocalLightSceneProxy* Light : Scene.GetLocalLights())
	{
		if (!Light->IsEnabled() || !Light->IsShown(View.bGameView)) continue;
		FLightRenderParameters Parameters{};
		Light->GetLightShaderParameters(Parameters);
		const int32 Index = RenderData.VisibleLights.Add(Parameters);
		RenderData.VisibleLightOwners.Add(Light->GetLightComponent());
		if (Light->GetLightComponent() == RenderData.PreviewLight) RenderData.PreviewLightIndex = Index;
	}

	RenderData.DirectionalLightShadowMapSize = Options.DirectionalShadowMapSize;
	RenderData.PointLightShadowMapSize = Options.PointShadowMapSize;
	RenderData.SpotLightShadowMapSize = Options.SpotShadowMapSize;

	RenderData.CurrentCasterRevision = Scene.GetShadowCasterRevision();
	RenderData.Commit(View.FrustumCorners, View.NearClip, View.FarClip, 4, bLightUploadSucceeded);
}

bool FSceneRenderer::BindDeferredLighting(FRenderer* Renderer, uint32 Width, uint32 Height, EPcfMode PcfMode)
{
	FSceneLightResources& LightResources = RenderData.ViewLightResources;

	FConstantBuffer* DefferedConstants = LightResources.GetDeferredConstants();

	if (!bLightUploadSucceeded || !DefferedConstants)
	{
		return false;
	}

	FTexture2D* LightShadowAtlas = LightResources.GetShadowMapAtlas();
	FStructuredBuffer* LightShadows = LightResources.GetLightShadows();
	
	FDeferredLightConstants Constants{};
	Constants.InverseViewProjection = View.ViewProjectionMatrix.Inverse();
	Constants.CameraPosition = FVector4(View.ViewLocation.X, View.ViewLocation.Y, View.ViewLocation.Z, View.bIsPerspective ? 1.0f : 0.0f);
	Constants.CameraForward = FVector4(View.ViewForward.X, View.ViewForward.Y, View.ViewForward.Z, 0.0f);
	
	const FSkyLightEnvironment* Sky = RenderData.bHasSkyLight ? World->GetScene().FindSkyLightEnvironment(RenderData.SkyLight.SourcePath) : nullptr;
	Constants.Ambient = Sky ? FAmbientLightRenderData{ FVector4(0.0f) } : RenderData.AmbientLight; // 지금 환경광 계산이 ambient + skylight로 되어있어서 sky 잇으면 ambient 0으로 해줘야됨
	Constants.SkyColorIntensity = Sky ? RenderData.SkyLight.ColorIntensity : FVector4(0.f);
	Constants.ShadowAtlasWidth = LightShadowAtlas ? LightShadowAtlas->GetWidth() : 0;
	Constants.ShadowAtlasHeight = LightShadowAtlas ? LightShadowAtlas->GetHeight() : 0;
	Constants.CascadeShadowCount = RenderData.CascadeShadowCount;
	Constants.PcfMode = static_cast<uint32>(PcfMode);
	std::memcpy(Constants.CascadeShadowDistances, RenderData.CascadeShadowDistances, sizeof(RenderData.CascadeShadowDistances));

	std::memcpy(Constants.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
	std::memcpy(Constants.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets)); // LightOffset은 각 조명별로 시작하는 위치
	std::memcpy(Constants.LightShadowOffsets, RenderData.LightShadowOffsets, sizeof(RenderData.LightShadowOffsets));

	Constants.ViewportSize = FVector4(static_cast<float>(Width), static_cast<float>(Height), 0.0f, 0.0f);
	Renderer->UpdateConstant(DefferedConstants, &Constants, sizeof(Constants));
	Renderer->BindConstant(0, DefferedConstants, EShaderBindFlagBits::Pixel);
	Renderer->BindStructured(3, RenderData.HasLights() ? LightResources.GetSceneLights() : nullptr, EShaderBindFlagBits::Pixel);
	Renderer->BindStructured(4, LightShadows, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(5, LightShadowAtlas, EShaderBindFlagBits::Pixel);

	Renderer->BindSampler(2, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
	Renderer->BindSampler(3, ESamplerState::ShadowComparison, EShaderBindFlagBits::Pixel);

	return true;
}

bool FSceneRenderer::BindForwardLighting(FRenderer* Renderer, const FSceneRenderOptions& Options)
{
	FSceneLightResources& LightResources = RenderData.ViewLightResources;

	FConstantBuffer* ForwardConstants = LightResources.GetForwardConstants();

	if (!bLightUploadSucceeded || !ForwardConstants)
	{
		return false;
	}
	
	FPipelineState* BasePSO = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_Forward);
	FShaderProgram* Program = FRenderResourceManager::GetForwardShaderProgram(Options.LightingModel);
	if (!BasePSO || !Program)
	{
		return false;
	}

	FTexture2D* LightShadowAtlas = LightResources.GetShadowMapAtlas();
	FStructuredBuffer* LightShadows = LightResources.GetLightShadows();

	FForwardLightingConstants Constants{};
	
	const FSkyLightEnvironment* Sky = RenderData.bHasSkyLight ? World->GetScene().FindSkyLightEnvironment(RenderData.SkyLight.SourcePath) : nullptr;
	Constants.Ambient = Sky ? FAmbientLightRenderData{ FVector4(0.0f) } : RenderData.AmbientLight;
	Constants.SkyColorIntensity = Sky ? RenderData.SkyLight.ColorIntensity : FVector4(0.f);
	Constants.CameraPosition = FVector4(View.ViewLocation.X, View.ViewLocation.Y, View.ViewLocation.Z, 1.0f);
	Constants.ShadowAtlasWidth = LightShadowAtlas ? LightShadowAtlas->GetWidth() : 0;
	Constants.ShadowAtlasHeight = LightShadowAtlas ? LightShadowAtlas->GetHeight() : 0;
	Constants.CascadeShadowCount = RenderData.CascadeShadowCount;
	Constants.PcfMode = static_cast<uint32>(Options.PcfMode);
	
	std::memcpy(Constants.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
	std::memcpy(Constants.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets)); // LightOffset은 각 조명별로 시작하는 위치
	std::memcpy(Constants.LightShadowOffsets, RenderData.LightShadowOffsets, sizeof(RenderData.LightShadowOffsets));
	std::memcpy(Constants.CascadeShadowDistances, RenderData.CascadeShadowDistances, sizeof(RenderData.CascadeShadowDistances));

	Renderer->UpdateConstant(ForwardConstants, &Constants, sizeof(Constants));
	Renderer->BindConstant(1, ForwardConstants, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	Renderer->BindCube(11, Sky ? Sky->GetIrradianceCube() : nullptr, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	Renderer->BindStructured(13, LightShadows, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(14, LightShadowAtlas, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	FForwardLightTileGrid& Tiles = LightResources.GetTiles();
	Tiles.PreparePixelShader(Renderer, bTilesBuilt);
	Renderer->BindPixelConstant(3, Tiles.GetPixelConstants());
	const std::array<ID3D11ShaderResourceView*, 3> TileSRVs = Tiles.GetPixelResources();
	Renderer->BindRawPixelResources(9, std::span<ID3D11ShaderResourceView* const>(TileSRVs.data(), 2));
	Renderer->BindRawPixelResources(12, std::span<ID3D11ShaderResourceView* const>(TileSRVs.data() + 2, 1));
	Renderer->BindStructured(8, RenderData.HasLights() && LightResources.GetSceneLights() && LightResources.GetSceneLights()->GetSRV() ? LightResources.GetSceneLights() : nullptr, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	Renderer->BindSampler(1, ESamplerState::LinearClamp, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	Renderer->BindSampler(2, ESamplerState::PointClamp, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	Renderer->BindSampler(3, ESamplerState::ShadowComparison, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	return true;
}

void FSceneRenderer::UnbindForwardLighting(FRenderer* Renderer)
{
	const std::array<ID3D11ShaderResourceView*, 2> NullSRVs{};
	Renderer->BindRawPixelResources(9, NullSRVs);
	Renderer->BindRawPixelConstant(3, nullptr);
	Renderer->UnbindResources(12, 3, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderData.ViewLightResources.GetTiles().ResetPixelState();
	Renderer->UnbindResources(8, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	Renderer->UnbindResources(11, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
}

bool FSceneRenderer::AddWorldNormalPass(FRenderGraphBuilder& Graph, FRenderer* Renderer,
	FSceneViewTargets& Targets, const FSceneRenderOptions& Options)
{
	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor");
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth) return false;

	FRenderGraphRasterPassDesc Pass{};
	Pass.Name = "WorldNormal";
	Pass.ColorAttachments = { { SceneColor, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 1.0f } } };
	Pass.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
	return Graph.AddRasterPass(std::move(Pass), [this, Renderer, bDrawPrimitives = Options.bDrawPrimitives]
		{
			if (!bDrawPrimitives) return;
			FMeshPassProcessor Processor;
			TArray<FMeshDrawBatch> Batches;
			bCorePassSucceeded = Processor.Build(View, RenderData.RenderQueue,
				&RenderData, EMeshPassType::WorldNormal, Batches);
			if (bCorePassSucceeded)
				Renderer->DrawMeshes(Batches);
		});
}

bool FSceneRenderer::AddCorePasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
	const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox)
{
	if (!World || !Renderer || !Targets.SceneColor || !Targets.Depth)
		return false;
	const uint32 Width = Targets.SceneColor->GetWidth();
	const uint32 Height = Targets.SceneColor->GetHeight();
	if (!Width || !Height || !Targets.IsValidFor(Width, Height))
		return false;

	bCorePassSucceeded = true;
	bTilesBuilt = false;
	if (Options.bShowWorldNormal)
		return AddWorldNormalPass(Graph, Renderer, Targets, Options);

	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor"); // Texture Handle 리턴 해줌
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth)
		return false;

	const FRenderGraphTextureDesc HDRDesc{ Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT };
	const FRenderGraphTextureRef LightingHDR = Graph.CreateTexture(HDRDesc, "LightingHDR");
	if (!LightingHDR)
		return false;
	
	PrepareDrawData(Renderer, RenderData);

	// HDR 파노라마를 물체보다 먼저 그려 두고, 최종 ToneMap에서 함께 인코딩한다.
	FRenderGraphRasterPassDesc Sky{};
	Sky.Name = "Skybox";
	Sky.ColorAttachments = {
		{ LightingHDR, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 1.0f } }
	};
	if (!Graph.AddRasterPass(std::move(Sky), [this, Skybox]
		{
			if (bCorePassSucceeded && Skybox)
				Skybox->OnRender(View.ViewProjectionMatrix, View.ViewLocation);
		})) return false;

	// NOTE: 셰도우 맵 패스를 등록한다.
	FTexture2D* ShadowMapAtlas = RenderData.ViewLightResources.GetShadowMapAtlas();

	const FRenderGraphTextureRef ShadowMapRef = Graph.ImportTexture(ShadowMapAtlas, "ShadowMapAtals");
	if (!ShadowMapRef)
	{
		return false;
	}

	if (ShadowMapAtlas && RenderData.HasLights())
	{
		FRenderGraphRasterPassDesc Shadows{};
		Shadows.Name = "Shadows";
		Shadows.DepthAttachment = { ShadowMapRef, ERenderGraphLoadAction::Load, 1.0f, 0 };

		auto ShadowMapPassHandler = [this, Renderer, &Graph, ShadowMapRef]() {
			if (!bCorePassSucceeded) return;

			FTexture2DArray* DepthArray = static_cast<FTexture2DArray*>(Graph.Resolve(ShadowMapRef));
			const bool bAnyDirty = std::find(RenderData.ShadowNeedsRender.begin(),
				RenderData.ShadowNeedsRender.end(), uint8(1)) != RenderData.ShadowNeedsRender.end();
			if (!bAnyDirty) return;

			FMeshPassProcessor Processor;
			TArray<FMeshDrawBatch> Batched;
			Processor.Build(RenderData.ShadowCasterProxies, EPSOType::StaticMesh_LightShadow, Batched);

			// Retain clean tiles. For dirty tiles, clear only their rectangle, then rerender that Shadow View.
			for (int32 i = 0; i < RenderData.LightShadows.Num(); ++i)
			{
				if (i >= RenderData.ShadowNeedsRender.Num() || !RenderData.ShadowNeedsRender[i]) continue;
				const FLightShadowRenderData& LightShadow = RenderData.LightShadows[i];

				RenderCommand::SetRenderTargets(nullptr, 0, DepthArray->GetDSV(LightShadow.AtlasIndex));
				if (!RenderCommand::ClearDepthStencilRect(DepthArray, LightShadow.AtlasIndex,
					static_cast<uint32>(LightShadow.AtlasRect.X), static_cast<uint32>(LightShadow.AtlasRect.Y),
					static_cast<uint32>(LightShadow.AtlasRect.Width), static_cast<uint32>(LightShadow.AtlasRect.Height), 1.0f))
				{
					HTR_LOG(Error, "Partial Shadow Atlas depth clear is unsupported or failed");
					bCorePassSucceeded = false;
					assert(false && "Partial Shadow Atlas clear unavailable; refusing stale shadow data");
					return;
				}
				RenderCommand::SetViewport(static_cast<uint32>(LightShadow.AtlasRect.X),
					static_cast<uint32>(LightShadow.AtlasRect.Y), static_cast<uint32>(LightShadow.AtlasRect.Width),
					static_cast<uint32>(LightShadow.AtlasRect.Height));
				Renderer->UploadViewMatrix(LightShadow.ViewProjection);
				Renderer->DrawMeshes(Batched);

				RenderData.MarkShadowRendered(i);

				++RenderData.RenderStats.ShadowViewsRedrawn;
			}
			};

		if (!Graph.AddRasterPass(std::move(Shadows), ShadowMapPassHandler))
		{
			return false;
		}
	}

	if (Options.RenderingPath == ERenderingPath::Deferred) // Deferred rendering
	{
		const FRenderGraphTextureDesc GBufferDesc{ Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM };
		const FRenderGraphTextureRef BaseColor = Graph.CreateTexture(GBufferDesc, "BaseColorMetallic"); // 임시 텍스쳐 생성
		const FRenderGraphTextureRef Normal = Graph.CreateTexture(HDRDesc, "NormalRoughness");
		if (!BaseColor || !Normal)
			return false;

		if (Options.bDrawPrimitives)
		{
			FRenderGraphRasterPassDesc GBuffer{};
			GBuffer.Name = "GBuffer";
			GBuffer.ColorAttachments = {
				{ BaseColor, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 0.0f } },
				{ Normal, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 0.0f } }
			}; // GBuffer Pass 에는 BaseColor 와 Normal Texture가 Clear된 상태로 필요하다

			GBuffer.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 }; // GBuffer Pass 에는 Depth Texture가 Clear된 상태로 필요하다

			if (!Graph.AddRasterPass(std::move(GBuffer), [this, Renderer]
				{
					TArray<FMeshDrawBatch> Batches;
					FMeshPassProcessor Processor;
					bCorePassSucceeded = Processor.Build(View, RenderData.RenderQueue,
						&RenderData, EMeshPassType::GBuffer, Batches);
					if (bCorePassSucceeded)
					{
						Renderer->UploadViewMatrix(View.ViewProjectionMatrix);
						Renderer->DrawMeshes(Batches);
					}
				})) return false;

			FRenderGraphRasterPassDesc LightingPass{};
			LightingPass.Name = "DeferredLighting";
			LightingPass.ReadTextures = { BaseColor, Normal, Depth };
			LightingPass.ColorAttachments = {
				{ LightingHDR, ERenderGraphLoadAction::Load }
			};
			if (!Graph.AddRasterPass(std::move(LightingPass), [this, Renderer, &Graph, BaseColor, Normal, Depth, Width, Height, PcfMode = Options.PcfMode, bShowShadow = Options.bShowShadow,
				ShadowDepthBias = Options.ShadowDepthBias]
				{
					if (!bCorePassSucceeded) return;
					if (!BindDeferredLighting(Renderer, Width, Height, PcfMode))
					{
						bCorePassSucceeded = false;
						return;
					}

					FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::DeferredLighting);
					if (Pipeline && Pipeline->Shader && Pipeline->Shader->VertexShader && Pipeline->Shader->PixelShader)
					{
						RenderCommand::BindPipelineState(*Pipeline);
						RenderCommand::BindVertexBuffer(nullptr);
						RenderCommand::Draw(3);
					}
					else
					{
						bCorePassSucceeded = false;
					}
					Renderer->UnbindResources(3, 2, EShaderBindFlagBits::Pixel);
					Renderer->BindConstant(1, nullptr, EShaderBindFlagBits::Pixel);
				})) return false;
		}
		else
		{
			FRenderGraphRasterPassDesc ClearDepth{};
			ClearDepth.Name = "ClearSceneDepth";
			ClearDepth.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
			if (!Graph.AddRasterPass(std::move(ClearDepth), [] {})) return false;
		}
	}
	else // Forward Rendering
	{
		ForwardBatches.Reset();
		if (Options.bDrawPrimitives)
		{
			FMeshPassProcessor Processor;
			if (!Processor.Build(View, RenderData.RenderQueue, &RenderData,
				EMeshPassType::Forward, ForwardBatches, Options.LightingModel)) return false;
		}
		// 현재 업로드된 광원 정보를 기존 그리드에 전달한다.
		FForwardLightTileGrid& Tiles = RenderData.ViewLightResources.GetTiles();
		FStructuredBuffer* SceneLights = RenderData.ViewLightResources.GetSceneLights();
		FForwardLightingConstants GridLighting{};
		std::memcpy(GridLighting.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
		std::memcpy(GridLighting.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets));

		// 픽셀 조명 경로에서만 목록을 사용한다.
		const ELightCullingMode CullingMode = !View.bIsWireframe &&	Options.LightingModel != ELightingModel::Gouraud ? Options.LightCullingMode : ELightCullingMode::Off;

		// Off도 이번 프레임의 화면 정보와 입력 광원 개수를 갱신한다.
		bool bUseDepthPrepass = false;
		const bool bGridPrepared = Options.bDrawPrimitives && bLightUploadSucceeded &&
			Tiles.PrepareBuild(View, GridLighting, SceneLights, Targets.Depth.get(), CullingMode);

		if (bGridPrepared && CullingMode != ELightCullingMode::Off && (RenderData.HasSpotLights() || RenderData.HasPointLights()))
		{
			FPipelineState* DepthPipeline = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_DepthOnly);
			if (!DepthPipeline || !DepthPipeline->Shader || !DepthPipeline->Shader->PixelShader) return false;
			bUseDepthPrepass = true;

			// 기존 프리패스를 유지해 ForwardOpaque에서 깊이를 재사용한다.
			FRenderGraphRasterPassDesc Prepass{};
			Prepass.Name = "DepthPrepass";
			Prepass.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
			if (!Graph.AddRasterPass(std::move(Prepass), [this, Renderer, DepthPipeline]
				{
					if (ForwardBatches.IsEmpty()) return;

					// 실제 메시 패스와 같은 VS를 사용해 깊이 계산을 일치시킨다.
					FShaderProgram DepthProgram(ForwardBatches.Front().Pipeline.Shader->VertexShader, DepthPipeline->Shader->PixelShader);
					TArray<FMeshDrawBatch> DepthBatches = ForwardBatches;
					for (FMeshDrawBatch& Batch : DepthBatches)
					{
						Batch.Pipeline = *DepthPipeline;
						Batch.Pipeline.Shader = &DepthProgram;
					}
					Renderer->DrawMeshes(DepthBatches);
				})) return false;

			// 선택한 알고리즘에 맞는 컴퓨트 패스를 등록한다.
			FRenderGraphComputePassDesc Culling{};
			Culling.Name = CullingMode == ELightCullingMode::Clustered
				? "ClusterLightCulling" : "TileLightCulling";
			Culling.ReadResources = { SceneLights->GetBuffer() };
			if (CullingMode == ELightCullingMode::Tiled)
				Culling.ReadResources.push_back(Targets.Depth->GetRawPtr());

			const std::array<ID3D11Resource*, 4> Writes = Tiles.GetWriteResources();
			Culling.WriteResources.assign(Writes.begin(), Writes.end());
			if (!Graph.AddComputePass(std::move(Culling),
				[this, Renderer, SceneLights, DepthTexture = Targets.Depth.get()]
				{
					if (bCorePassSucceeded)
						bTilesBuilt = RenderData.ViewLightResources.GetTiles().ExecuteBuild(Renderer, DepthTexture, SceneLights);
				})) 
				return false;
		}
		FRenderGraphRasterPassDesc Forward{};
		Forward.Name = "ForwardOpaque";
		Forward.ColorAttachments = {
			{ LightingHDR, ERenderGraphLoadAction::Load }
		};
		Forward.DepthAttachment = { Depth, bUseDepthPrepass ? ERenderGraphLoadAction::Load : ERenderGraphLoadAction::Clear, 1.0f, 0 };
		if (!Graph.AddRasterPass(std::move(Forward), [this, Renderer, LightingOptions = Options, bUseDepthPrepass]
		{
			if (!LightingOptions.bDrawPrimitives) return;
			bCorePassSucceeded = BindForwardLighting(Renderer, LightingOptions);
			if (!bCorePassSucceeded) return;
			if (bUseDepthPrepass)
			{
				for (FMeshDrawBatch& Batch : ForwardBatches)
				{
					Batch.Pipeline.DepthStencilState = EDepthStencilState::PrepassReadOnly;
				}
			}
			Renderer->DrawMeshes(ForwardBatches);
			UnbindForwardLighting(Renderer);
		})) return false;
	}

	FRenderGraphRasterPassDesc ToneMap{};
	ToneMap.Name = "ToneMap";
	ToneMap.ReadTextures = { LightingHDR };
	ToneMap.ColorAttachments = { { SceneColor, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 1.0f } } };
	if (!Graph.AddRasterPass(std::move(ToneMap), [this, Renderer, &Graph, LightingHDR]
		{
			if (bCorePassSucceeded)
			{
				if (FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::ToneMap);
					Pipeline && Pipeline->Shader && Pipeline->Shader->VertexShader && Pipeline->Shader->PixelShader)
				{
					RenderCommand::BindPipelineState(*Pipeline);
					RenderCommand::BindVertexBuffer(nullptr);
					RenderCommand::Draw(3);
				}
			}
		})) return false;

	return true;
}

bool FSceneRenderer::AddViewportPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer,
	FSceneViewTargets& Targets, const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox,
	const FScenePassCallbacks& Callbacks) // 뷰포트에 그려져야 되는 패스들을 옵션에 따라 정해진 순서로 Graph에 등록
{
	FSceneRenderOptions CoreOptions = Options;
	if (Options.bShowLightCulling)
	{
		// 디버그 보기에서 현재 뷰의 실제 타일 목록을 생성한다.
		CoreOptions.RenderingPath = ERenderingPath::Forward;
		CoreOptions.LightingModel = ELightingModel::Lambert;
	}
	if (!AddCorePasses(Graph, Renderer, Targets, CoreOptions, Skybox)) // 기본적으로 그려져야 되는 Core Pass 들
		return false;
	
	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor");
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth) return false;
	bHasFog = false;
	bFXAAPassSucceeded = true;

	if (!Options.bShowWorldNormal && Options.bDrawPrimitives && Callbacks.FireBall)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("FireBall", SceneColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), Callbacks.FireBall)) return false;
	}
	if (!Options.bShowWorldNormal && Options.bDrawPrimitives && Options.bEnableFog && Callbacks.HeightFog)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("HeightFog", SceneColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), [this, DrawFog = Callbacks.HeightFog]
			{
			bHasFog = DrawFog();
			})) return false;
	}
	if (!Options.bShowWorldNormal && Options.bDrawPrimitives)
	{
		if (!Graph.AddRasterPass(MakeViewportPass("Translucent", SceneColor, Depth),
			[this, Renderer, GetFog = Callbacks.TranslucentFogConstants]
			{
			RenderTranslucent(Renderer, GetFog ? GetFog(bHasFog) : nullptr);
			})) return false;
	}
	if (!Options.bShowWorldNormal && Callbacks.SceneText &&
		!Graph.AddRasterPass(MakeViewportPass("SceneText", SceneColor, Depth), Callbacks.SceneText))
		return false;

	if (!Options.bShowWorldNormal && Options.bShowSceneDepth && Callbacks.DepthVisualization)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("SceneDepthVisualization", SceneColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), Callbacks.DepthVisualization)) return false;
	}

	FRenderGraphTextureRef FinalColor = SceneColor;
	if (!Options.bShowWorldNormal && Callbacks.FXAAOutput && Callbacks.FXAA)
	{
		FinalColor = Graph.ImportTexture(Callbacks.FXAAOutput, "FXAAOutput");
		if (!FinalColor) return false;
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("FXAA", FinalColor);
		Pass.ReadTextures = { SceneColor };
		if (!Graph.AddRasterPass(std::move(Pass), [this, ApplyFXAA = Callbacks.FXAA]
			{
			bFXAAPassSucceeded = ApplyFXAA();
			})) return false;
	}

	if (Options.bShowLightCulling && Options.bDrawPrimitives && !Options.bShowWorldNormal)
	{
		FRenderGraphRasterPassDesc Debug = MakeViewportPass("LightCullingDebug", FinalColor);
		Debug.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Debug), [this, Renderer, &Graph, Depth]
			{
				if (!bCorePassSucceeded) return;
				FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::LightCullingDebug);
				if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader || !Pipeline->Shader->PixelShader)
				{
					bCorePassSucceeded = false;
					return;
				}
				FForwardLightTileGrid& Tiles = RenderData.ViewLightResources.GetTiles();
				Tiles.PreparePixelShader(Renderer, bTilesBuilt);
				Renderer->BindPixelConstant(3, Tiles.GetPixelConstants());
				const std::array<ID3D11ShaderResourceView*, 3> TileResources = Tiles.GetPixelResources();
				Renderer->BindRawPixelResources(9, std::span<ID3D11ShaderResourceView* const>(TileResources.data(), 1));
				RenderCommand::BindPipelineState(*Pipeline);
				RenderCommand::BindVertexBuffer(nullptr);
				RenderCommand::Draw(3);
				Renderer->UnbindResources(9, 1, EShaderBindFlagBits::Pixel);
				Renderer->BindPixelConstant(3, nullptr);
				Tiles.ResetPixelState();
			})) return false;
	}

	const FSceneOverlayCallbacks& Overlay = Callbacks.Overlays;
	if (!AddOverlayPass(Graph, "EditorLines", FinalColor, Depth, Overlay.EditorLines) ||
		!AddOverlayPass(Graph, "EditorGrid", FinalColor, Depth, Overlay.EditorGrid) ||
		!AddOverlayPass(Graph, "EditorOutline", FinalColor, Depth, Overlay.EditorOutline) ||
		!AddOverlayPass(Graph, "EditorGizmo", FinalColor, Depth, Overlay.EditorGizmo, ERenderGraphLoadAction::Clear) ||
		!AddOverlayPass(Graph, "SIEOutline", FinalColor, Depth, Overlay.SIEOutline) ||
		!AddOverlayPass(Graph, "SIEGizmo", FinalColor, Depth, Overlay.SIEGizmo, ERenderGraphLoadAction::Clear))
		return false;
	if (Overlay.bClearEditorDepth)
	{
		FRenderGraphRasterPassDesc ClearDepth{};
		ClearDepth.Name = "EditorClearDepth";
		ClearDepth.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
		if (!Graph.AddRasterPass(std::move(ClearDepth), [] {})) return false;
	}
	if (!AddOverlayPass(Graph, "EditorUUID", FinalColor, Depth, Overlay.EditorUUID)) return false;
	return true;
}


// 반투명 렌더링
void FSceneRenderer::RenderTranslucent(FRenderer* Renderer, FConstantBuffer* FogConstants)
{
	if (!Renderer)
	{
		return;
	}

	Renderer->BindPixelConstant(3, FogConstants);
	PrepareDrawData(Renderer, RenderData);
	TArray<FMeshDrawBatch> Batches;
	FMeshPassProcessor Processor;
	if (Processor.Build(View, RenderData.RenderQueue, &RenderData,
		EMeshPassType::Translucent, Batches))
	{
		Renderer->UploadViewMatrix(View.ViewProjectionMatrix);
		Renderer->DrawMeshes(Batches);
	}
	Renderer->BindPixelConstant(3, nullptr);
}

void FSceneRenderer::GatherRenderPackets(FScene& Scene, FSceneRenderData& Data,
	const FViewContext& View, const FFrustumPlanes* Frustum, FRenderer* Renderer) //여기서 꼭 renderer를 매개변수로 받아야 할까?
{
	Data.ResetFrame();
	for (FPrimitiveSceneProxy* Proxy : Scene.PrimitiveSceneProxies)
	{
		if (Proxy->IsShown(View.bGameView) && Proxy->bCastsShadow && Proxy->GetRenderState() && Proxy->GetMesh())
			Data.ShadowCasterProxies.Add(Proxy);
	}
	FRenderQueue& RenderQueue = Data.RenderQueue;
	FRenderStats& RenderStats = Data.RenderStats;
	auto& VisibleProxies = Data.VisibleProxies;
	auto& LODInputs = Data.LODInputs;
	auto& SelectedLODs = Data.SelectedLODs;
	auto& GatherChunks = Data.GatherChunks;
	using FGatherChunk = FSceneRenderData::FGatherChunk;

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// 표시 상태는 프록시 캐시로 판정해 오클루전과 드로우 수집 전에 제외한다.
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy)
		{
			if (Proxy->IsShown(View.bGameView)) VisibleProxies.Add(Proxy);
		};

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&) { return EBVHCullResult::Inside; }, Visit);
		}
	}

	RenderStats.TotalPrimitives = Scene.PrimitiveSceneProxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
	const uint8* OccludedMask = nullptr;
	if (Renderer && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), View))
		{
			const auto& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
					if (!Occluded[i])
						VisibleProxies[Kept++] = VisibleProxies[i];
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	if (Renderer) Data.ObjectConstants.SetNum(MaxSlots);
	const bool bPrepareObjectSlots = Renderer != nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	const uint32 NumThreads = Tasks::FTaskScheduler::Get().GetNumWorkers() + 1;
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, NumThreads * 4);

	if (GatherChunks.Num() < ChunkCount)
		GatherChunks.SetNum(ChunkCount);     // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.

	// Renderer가 있으면 불투명 스태틱 메시는 패킷 대신 조각별 묶음에 작은 항목으로 넣는다.
	// 동일 바인딩의 개별 드로우를 묶어 패킷 복사와 항목별 정렬을 줄인다.
	const bool bStaticGroups = Renderer != nullptr;

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		LODInputs.Reserve(VisibleProxies.Num());
		for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
			LODInputs.Add({Proxy->GetLODSphere(), Proxy->GetRenderState()});
		SelectLODs(LODInputs, View, SelectedLODs);
		Tasks::ParallelForChunks(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD)
								return Last;
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()) continue;

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = SelectedLODs[VisibleIndex];
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (bPrepareObjectSlots)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						const FPerObjectConstants Constants =
							MakePerObjectConstants(Proxy->GetLocalToWorld());

						Data.ObjectConstants[VisibleIndex] = Constants;

						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->PSOType == EPSOType::StaticMesh_Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.Mesh = Mesh;
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				}
			});

	// 이 뷰의 스태틱 드로우 수를 세고, 실제 렌더링할 때에만 해당 묶음을 바인딩한다.
		uint32 StaticDraws = 0;
		if (Renderer)
		{
			for (uint32 c = 0; c < ChunkCount; ++c)
			{
				StaticDraws += GatherChunks[c].StaticDrawCount;
			}
		}

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
			TotalPackets += GatherChunks[c].Packets.Num();

		RenderQueue.Reserve(TotalPackets + 256);             // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
			if (GatherChunks[c].Packets.Num() > 0)
				RenderQueue.Append(GatherChunks[c].Packets);  // 조각 순서대로 이어 붙이기

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
					continue;

				const uint32 FirstNew = RenderQueue.Num();

				// 프록시 캐시가 없는 스태틱 메시는 기존처럼 LOD를 골라 제출하고, 그 외는 컴포넌트에 맡긴다.
				UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive);
				if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
				{
					const uint32 LOD = SelectedLODs[VisibleIndex];
					StaticMeshComponent->SubmitToRenderQueue(RenderQueue, LOD);
				}
				else
				{
					Primitive->SubmitToRenderQueue(RenderQueue, View);
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					if (!bPrepareObjectSlots || NextExtraSlot >= MaxSlots) break;
					FRenderPacket& Packet = RenderQueue[p];
					const FMatrix& Model = Packet.Proxy ? Packet.Proxy->GetLocalToWorld() : Packet.Model ? *Packet.Model : FMatrix::Identity;
					Data.ObjectConstants[NextExtraSlot] = MakePerObjectConstants(Model);
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}

		if (bPrepareObjectSlots) Data.ObjectConstants.SetNum(NextExtraSlot);
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles) RenderStats.Triangles += T;
	}
}


void FSceneRenderer::PrepareDrawData(FRenderer* Renderer, const FSceneRenderData& Data)
{
	if (!Renderer) return;
	if (uint8* Destination = Renderer->BeginObjectConstants(static_cast<uint32>(Data.ObjectConstants.Num())))
	{
		for (int32 Index = 0; Index < Data.ObjectConstants.Num(); ++Index)
			std::memcpy(Destination + static_cast<size_t>(Index) * ObjectSlotBytes,
				&Data.ObjectConstants[Index], sizeof(FPerObjectConstants));
		Renderer->EndObjectConstants();
	}
}
