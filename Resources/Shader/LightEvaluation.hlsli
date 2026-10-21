#ifndef LIGHT_EVALUATION_INCLUDED
#define LIGHT_EVALUATION_INCLUDED

#include "LightData.hlsli"

// hwang의 Ambient/Lambert/Blinn-Phong 식을 Forward와 Deferred가 공유한다.
struct FLightResult
{
    float3 Diffuse;
    float3 Specular;
};

struct FShadowCoordinates
{
    float Depth;
    float2 CenterUV;
    float2 DepthSlope;
    float2 MinUV;
    float2 MaxUV;
};

// Best-candidate Poisson samples within the unit disk.
static const uint PoissonSampleCount = 32;
static const float2 PoissonDisk[PoissonSampleCount] =
{
    float2(-0.68341047, -0.26169425),
    float2(0.92626810, 0.36191782),
    float2(-0.22649025, 0.95149655),
    float2(0.49230477, -0.86085707),
    float2(0.08114958, 0.14360106),
    float2(-0.26010181, -0.93738593),
    float2(-0.85894994, 0.50310161),
    float2(0.43717422, 0.85403625),
    float2(0.93843458, -0.32464634),
    float2(0.09392068, -0.43937528),
    float2(-0.37764098, 0.36231411),
    float2(0.49220528, -0.12107827),
    float2(-0.65549162, -0.71528620),
    float2(-0.24774403, -0.16076377),
    float2(0.46676082, 0.40285104),
    float2(0.05545897, 0.60007380),
    float2(-0.94426393, 0.08583122),
    float2(-0.30051113, -0.55052650),
    float2(-0.58954771, 0.78290908),
    float2(0.11683487, -0.93823656),
    float2(0.52408168, -0.49861045),
    float2(0.72293188, 0.65095385),
    float2(0.81920464, 0.00911533),
    float2(-0.57186791, 0.07081329),
    float2(0.11791642, 0.98891043),
    float2(-0.86593411, -0.48262448),
    float2(-0.25818098, 0.65025289),
    float2(0.20555281, -0.13129390),
    float2(-0.97871542, -0.20254113),
    float2(-0.03298199, -0.69704464),
    float2(0.78394164, -0.61996089),
    float2(0.27027231, -0.67373301)
};

float CalculateDistanceAttenuation(float DistanceSquared, float RadiusSquared)
{
    if (RadiusSquared <= 0.0f || DistanceSquared >= RadiusSquared || DistanceSquared < 1.0e-8f)
        return 0.0f;
    float RadiusFade = saturate(1.0f - DistanceSquared / RadiusSquared);
    return RadiusFade * RadiusFade / (1.0f + DistanceSquared);
}

float CalculateConeAttenuation(float CosAngle, float CosInner, float CosOuter)
{
    if (CosAngle <= CosOuter) return 0.0f;
    return saturate((CosAngle - CosOuter) / max(CosInner - CosOuter, 1.0e-8f));
}

void AccumulateLight(inout FLightResult Result, float3 N, float3 V, float3 L,
    float3 Radiance, float3 SpecularColor, float Shininess, bool bIncludeSpecular)
{
    float NoL = saturate(dot(N, L));
    Result.Diffuse += Radiance * NoL;
    if (bIncludeSpecular && NoL > 0.0f)
    {
        float3 H = L + V;
        H *= rsqrt(max(dot(H, H), 1.0e-8f));
        Result.Specular += Radiance * SpecularColor * pow(saturate(dot(N, H)), max(Shininess, 1.0f));
    }
}

float CalculateLinearDepth(float Z, float Near, float Far)
{
    return (Near * Far) / (Far - Z * (Far - Near));
}

float PCF(int SampleCount, FShadowCoordinates Coordinates, float2 FilterRadius, float Bias, Texture2DArray Atlas, uint AtlasIndex, SamplerComparisonState ShadowSampler)
{
    float Result = 1.0;
    
    SampleCount = min(SampleCount, PoissonSampleCount);
    
    int ValidSample = 0;
    float Sum = 0.0;
    for (uint i = 0; i < SampleCount; i++)
    {
        float2 Offset = PoissonDisk[i] * FilterRadius;
        float2 UV = Coordinates.CenterUV + Offset;
        
        if (any(UV < Coordinates.MinUV) || any(UV > Coordinates.MaxUV))
        {
            continue;
        }
        
        float ReceiverDepthAtSample = Coordinates.Depth + dot(Coordinates.DepthSlope, Offset);
        
        Sum += Atlas.SampleCmpLevelZero(ShadowSampler, float3(UV, AtlasIndex), ReceiverDepthAtSample - Bias).r;
        ValidSample++;
    }
            
    if (ValidSample > 0)
    {
        Result = Sum / (float) ValidSample;
    }
    
    return Result;
}

bool CalculateShadowCoordinates(float3 P, FLightShadowRenderData Shadow, uint AtlasWidth, uint AtlasHeight, out FShadowCoordinates Coordinates)
{
    float4 ClipPosition = mul(float4(P, 1.0), Shadow.ViewProjection);
    float3 ShadowPosition = ClipPosition.xyz / ClipPosition.w;
        
    float CenterU = ShadowPosition.x * 0.5 + 0.5;
    CenterU = (Shadow.AtlasRect.x + CenterU * Shadow.AtlasRect.z) / AtlasWidth;

    float CenterV = -ShadowPosition.y * 0.5 + 0.5;
    CenterV = (Shadow.AtlasRect.y + CenterV * Shadow.AtlasRect.w) / AtlasHeight;
    
    float2 CenterUV = float2(CenterU, CenterV);
    
    float2 UDelta = float2(ddx(CenterU), ddy(CenterU));
    float2 VDelta = float2(ddx(CenterV), ddy(CenterV));
    float2 DepthDelta = float2(ddx(ShadowPosition.z), ddy(ShadowPosition.z));
    float D = UDelta.x * VDelta.y - UDelta.y * VDelta.x;
    
    float USlope = 0.0;
    float VSlope = 0.0;
    if (D != 0.0)
    {
        USlope = (DepthDelta.x * VDelta.y - DepthDelta.y * VDelta.x) / D;
        VSlope = (UDelta.x * DepthDelta.y - UDelta.y * DepthDelta.x) / D;
    }
        
    if (ClipPosition.w <= 0.0 || ShadowPosition.x < -1.0 || ShadowPosition.x > 1.0 || ShadowPosition.y < -1.0 || ShadowPosition.y > 1.0 || ShadowPosition.z < 0 || ShadowPosition.z > 1)
    {
        return false;
    }
        
    float2 AtlasSize = float2(AtlasWidth, AtlasHeight);
    float2 MinUV = float2(Shadow.AtlasRect.xy + 0.5) / AtlasSize;
    float2 MaxUV = float2(Shadow.AtlasRect.xy + Shadow.AtlasRect.zw - 0.5) / AtlasSize;
    
    CenterUV = clamp(CenterUV, MinUV, MaxUV);
    
    Coordinates.Depth = ShadowPosition.z;
    Coordinates.CenterUV = CenterUV;
    Coordinates.DepthSlope = float2(USlope, VSlope);
    Coordinates.MinUV = MinUV;
    Coordinates.MaxUV = MaxUV;
        
    return true;
}

bool FindAverageBlockerDepth(FShadowCoordinates Coordinates, float2 SearchRadiusUV, float DepthBias, FLightShadowRenderData Shadow, bool bPerspective, Texture2DArray Atlas, SamplerState PointSampler, out float AverageBlockerDepth)
{
    AverageBlockerDepth = 0.0;
    
    int BlockerCount = 0;
    float BlockerDepthSum = 0.0f;

    // 무조건 중앙 샘플 포함
    float CenterDepth = Atlas.SampleLevel(PointSampler, float3(Coordinates.CenterUV, Shadow.AtlasIndex), 0).r;
    if (CenterDepth < Coordinates.Depth - DepthBias)
    {
        BlockerDepthSum += bPerspective ? CalculateLinearDepth(CenterDepth, Shadow.Near, Shadow.Far) : CenterDepth;
        BlockerCount++;
    }
    
    for (uint i = 0; i < PoissonSampleCount; i++)
    {
        float2 Offset = PoissonDisk[i] * SearchRadiusUV;
        float2 UV = Coordinates.CenterUV + Offset;
        
        if (any(UV < Coordinates.MinUV) || any(UV > Coordinates.MaxUV))
        {
            continue;
        }
        
        float ReceiverDepthAtSample = Coordinates.Depth + dot(Coordinates.DepthSlope, Offset);
        
        float SampledDepth = Atlas.SampleLevel(PointSampler, float3(UV, Shadow.AtlasIndex), 0).r;
        if (SampledDepth < ReceiverDepthAtSample - DepthBias)
        {
            BlockerDepthSum += bPerspective ? CalculateLinearDepth(SampledDepth, Shadow.Near, Shadow.Far) : SampledDepth;
            BlockerCount++;
        }
    }
    
    if (BlockerCount == 0)
    {
        return false;
    }

    AverageBlockerDepth = BlockerDepthSum / float(BlockerCount);
    return true;
}

float CalculateDirectionalShadowPCSS(float3 P, FLightRenderParameters Light, FLightShadowRenderData Shadow, Texture2DArray Atlas, uint AtlasWidth, uint AtlasHeight, SamplerState PointSampler, SamplerComparisonState ShadowSampler)
{    
    FShadowCoordinates Coordinates;
    if (!CalculateShadowCoordinates(P, Shadow, AtlasWidth, AtlasHeight, Coordinates))
    {
        return 1.0f;
    }
        
    const float DepthBias = 0.001f;
    
    float2 AtlasSize = float2(AtlasWidth, AtlasHeight);

    // Near에서 Reciever까지의 거리 연산
    float ReceiverDistanceFromNear = Coordinates.Depth * (Shadow.Far - Shadow.Near);
    
    // World 공간의 탐색 범위를 계산한다.
    float SearchRadiusWorld = Light.SourceSize * ReceiverDistanceFromNear;
    
    // VP에서 UV 변환 비율을 추출한다.
    float2 WorldToLocalUV = 0.5f * float2(
        length(float3(Shadow.ViewProjection[0][0], Shadow.ViewProjection[1][0], Shadow.ViewProjection[2][0])),
        length(float3(Shadow.ViewProjection[0][1], Shadow.ViewProjection[1][1], Shadow.ViewProjection[2][1]))
    );
    
    // World 공간의 탐색 반지름을 UV 공간으로 변경한다.
    float2 SearchRadiusUV = SearchRadiusWorld * WorldToLocalUV;
    
    // Atlas 내에서의 UV를 구한다.
    float2 LocalToAtlasUV = Shadow.AtlasRect.zw / AtlasSize;
    SearchRadiusUV *= LocalToAtlasUV;
    
    // Blocker를 탐색한다.
    float AverageBlockerDepth;
    if (!FindAverageBlockerDepth(Coordinates, SearchRadiusUV, DepthBias, Shadow, false, Atlas, PointSampler, AverageBlockerDepth))
    {
        return 1.0f;
    }
    
    // Blocker의 평균 깊이와 Reciever의 평균 깊이를 통해 reciever와 blocker의 거리를 구한다.
    float RecieverDist = max(Coordinates.Depth - AverageBlockerDepth, 0.0) * (Shadow.Far - Shadow.Near);
    
    // 필터 반지름과 그 값을 통해 FilterRadiusUV를 구한다.
    float FilterRadiusWorld = Light.SourceSize * RecieverDist;
    float2 FilterRadiusUV = FilterRadiusWorld * WorldToLocalUV * LocalToAtlasUV;
    
    return PCF(PoissonSampleCount, Coordinates, FilterRadiusUV, DepthBias, Atlas, Shadow.AtlasIndex, ShadowSampler);
}

float CalculatePointShadowPCSS(float3 P, FLightRenderParameters Light, FLightShadowRenderData Shadow, Texture2DArray Atlas, uint AtlasWidth, uint AtlasHeight, SamplerState PointSampler, SamplerComparisonState ShadowSampler)
{
    FShadowCoordinates Coordinates;
    if (!CalculateShadowCoordinates(P, Shadow, AtlasWidth, AtlasHeight, Coordinates))
    {
        return 1.0f;
    }
    
    const float DepthBias = 0.001f;
    
    float2 AtlasSize = float2(AtlasWidth, AtlasHeight);
    
    float ReceiverDepth = CalculateLinearDepth(Coordinates.Depth, Shadow.Near, Shadow.Far);
    
    // Near 평면 위의 발광체의 반지름을 구한다.
    float SearchRadiusAtNear = Light.SourceSize * (ReceiverDepth - Shadow.Near) / ReceiverDepth;
    
    // World 공간의 탐색 반지름을 UV 공간으로 변경한다.
    float SearchRadiusLocalUV = SearchRadiusAtNear / (2.0f * Shadow.Near);
    
    // Atlas 내에서의 UV를 구한다.
    float2 LocalToAtlasUV = Shadow.AtlasRect.zw / AtlasSize;
    float2 SearchRadiusUV = SearchRadiusLocalUV * LocalToAtlasUV;
    
    // Blocker를 탐색한다.
    float AverageBlockerDepth;
    if (!FindAverageBlockerDepth(Coordinates, SearchRadiusUV, DepthBias, Shadow, true, Atlas, PointSampler, AverageBlockerDepth))
    {
        return 1.0f;
    }
    
    // 월드 반 그림자의 반지름을 구한다.
    float FilterRadiusWorld = Light.SourceSize * max(ReceiverDepth - AverageBlockerDepth, 0.0f) / max(AverageBlockerDepth, 0.0001f);
    
    // 반 그림자를 UV 좌표계로 변경한다.
    float FilterRadiusLocalUV = FilterRadiusWorld / (2.0f * ReceiverDepth);
    float2 FilterRadiusUV = FilterRadiusLocalUV * LocalToAtlasUV;
    
    return PCF(PoissonSampleCount, Coordinates, FilterRadiusUV, DepthBias, Atlas, Shadow.AtlasIndex, ShadowSampler);
}

float CalculateSpotShadowPCSS(float3 P, FLightRenderParameters Light, FLightShadowRenderData Shadow, Texture2DArray Atlas, uint AtlasWidth, uint AtlasHeight, SamplerState PointSampler, SamplerComparisonState ShadowSampler)
{
    FShadowCoordinates Coordinates;
    if (!CalculateShadowCoordinates(P, Shadow, AtlasWidth, AtlasHeight, Coordinates))
    {
        return 1.0f;
    }
    
    const float DepthBias = 0.001f;
    
    float2 AtlasSize = float2(AtlasWidth, AtlasHeight);
    
    float ReceiverDepth = CalculateLinearDepth(Coordinates.Depth, Shadow.Near, Shadow.Far);
    float TanHalfFov = tan(acos(Light.OuterConeCos));
    
    // Near 평면 위의 발광체의 반지름을 구한다.
    float SearchRadiusAtNear = Light.SourceSize * (ReceiverDepth - Shadow.Near) / ReceiverDepth;
    
    // World 공간의 탐색 반지름을 UV 공간으로 변경한다.
    float SearchRadiusLocalUV = SearchRadiusAtNear / (2.0f * Shadow.Near * TanHalfFov);
    
    // Atlas 내에서의 UV를 구한다.
    float2 LocalToAtlasUV = Shadow.AtlasRect.zw / AtlasSize;
    float2 SearchRadiusUV = SearchRadiusLocalUV * LocalToAtlasUV;
    
    // Blocker를 탐색한다.
    float AverageBlockerDepth;
    if (!FindAverageBlockerDepth(Coordinates, SearchRadiusUV, DepthBias, Shadow, true, Atlas, PointSampler, AverageBlockerDepth))
    {
        return 1.0f;
    }
    
    // 월드 반 그림자의 반지름을 구한다.
    float FilterRadiusWorld = Light.SourceSize * max(ReceiverDepth - AverageBlockerDepth, 0.0f) / max(AverageBlockerDepth, 0.0001f);
    
    // 반 그림자를 UV 좌표계로 변경한다.
    float FilterRadiusLocalUV = FilterRadiusWorld / (2.0f * ReceiverDepth * TanHalfFov);
    float2 FilterRadiusUV = FilterRadiusLocalUV * LocalToAtlasUV;
    
    return PCF(PoissonSampleCount, Coordinates, FilterRadiusUV, DepthBias, Atlas, Shadow.AtlasIndex, ShadowSampler);
}

float CalculateShadow(float3 P, FLightRenderParameters Light, FLightShadowRenderData Shadow, Texture2DArray Atlas, uint AtlasWidth, uint AtlasHeight, SamplerState PointSampler, SamplerComparisonState ShadowSampler, float bReceiveShadow, uint PcfMode)
{
    [branch]
    if (bReceiveShadow < 0.5f)
    {
        return 1.0f;
    }

    if (PcfMode == 4)
    {
        // PCSSMode
        if (Light.Type == LIGHT_TYPE_DIRECTIONAL)
        {
            return CalculateDirectionalShadowPCSS(P, Light, Shadow, Atlas, AtlasWidth, AtlasHeight, PointSampler, ShadowSampler);
        }
        else if (Light.Type == LIGHT_TYPE_POINT)
        {
            return CalculatePointShadowPCSS(P, Light, Shadow, Atlas, AtlasWidth, AtlasHeight, PointSampler, ShadowSampler);
        }
        else if (Light.Type == LIGHT_TYPE_SPOT)
        {
            return CalculateSpotShadowPCSS(P, Light, Shadow, Atlas, AtlasWidth, AtlasHeight, PointSampler, ShadowSampler);
        }
    }
    
    FShadowCoordinates Coordinates;
    if (!CalculateShadowCoordinates(P, Shadow, AtlasWidth, AtlasHeight, Coordinates))
    {
        return 1.0f;
    }
        
    const float DepthBias = 0.001f;
        
    int SampleCount = 0;
    if (PcfMode == 0)
    {
        return Atlas.SampleCmpLevelZero(ShadowSampler, float3(Coordinates.CenterUV, Shadow.AtlasIndex), Coordinates.Depth - DepthBias).r;
    }
    else if (PcfMode == 1)
    {
        SampleCount = 8;
    }
    else if (PcfMode == 2)
    {
        SampleCount = 16;
    }
    else if (PcfMode == 3)
    {
        SampleCount = 32;
    }
    
    float2 TexelSize = 1.0f / float2(AtlasWidth, AtlasHeight);
    float RadiusTexels = (float) PcfMode;
    
    return PCF(SampleCount, Coordinates, TexelSize * RadiusTexels, DepthBias, Atlas, Shadow.AtlasIndex, ShadowSampler);
}

#endif
