#pragma once

#include "Components/LightComponent.h"
#include "SceneManagement.h"
#include "Render/LightRenderData.h"

// MSVC 최적화
// 가상함수 테이블과 패딩 방지
class FLightSceneProxyFixLayout
{
public:
	virtual ~FLightSceneProxyFixLayout() = default;
};

class FLightSceneProxy : public FLightSceneProxyFixLayout
{
public:
	FLightSceneProxy(const ULightComponent* InLightComponent)
		: LightComponent(InLightComponent) {}
	virtual ~FLightSceneProxy() = default;

	virtual void GetLightShaderParameters(FLightRenderParameters& OutParameters) const = 0;
	virtual void GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const = 0;

	const ULightComponent* GetLightComponent() const { return LightComponent; }
	bool IsVisible() const { return LightComponent ? LightComponent->IsVisible() : false; }
	bool IsEnabled() const { return LightComponent && LightComponent->GetIntensity() > 0.0f; }
	bool IsShown(bool bGameView) const { return LightComponent ? LightComponent->IsShown(bGameView) : false; }
protected:
	const ULightComponent* LightComponent = nullptr;

private:
	friend class FScene;
	FScene* Scene = nullptr;
	int32 PackedIndex = INDEX_NONE;
};

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
	void GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const override;
};

class FLocalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
};

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
	void GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const override;
};

class FSpotLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
	void GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const override;
};

class FRectLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
	void GetShadowShaderParameters(FShadowViewData& OutParameters, uint32 Index) const override;
};

class FAmbientLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FAmbientLightRenderData& GetRenderData() const { return RenderData; }

private:
	FAmbientLightRenderData RenderData{};
};

class FSkyLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FSkyLightRenderData& GetRenderData() const { return RenderData; }

private:
	FSkyLightRenderData RenderData{};
};
