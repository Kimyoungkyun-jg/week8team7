#ifndef LIGHT_EVALUATION_INCLUDED
#define LIGHT_EVALUATION_INCLUDED

// hwang의 Ambient/Lambert/Blinn-Phong 식을 Forward와 Deferred가 공유한다.
struct FLightResult
{
    float3 Diffuse;
    float3 Specular;
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

float3 GetCascadeDebugColor(float ViewDepth)
{
    if (ViewDepth < 10.0f)
        return float3(1.0f, 0.0f, 0.0f); // Cascade 0: 빨강

    if (ViewDepth < 30.0f)
        return float3(0.0f, 1.0f, 0.0f); // Cascade 1: 초록

    if (ViewDepth < 100.0f)
        return float3(0.0f, 0.0f, 1.0f); // Cascade 2: 파랑

    return float3(1.0f, 1.0f, 0.0f); // Cascade 3: 노랑
}

float CalculateShadow(float3 P, float4x4 VP, float4 AtlasRect, Texture2DArray Atlas, uint AtlasWidth, uint AtlasHeight, uint AtlasIndex, SamplerState Sampler)
{
    float Shadow = 1.0;
    
    float4 CoordPos = mul(float4(P, 1.0), VP);
    if (CoordPos.w > 0)
    {
        CoordPos.xyz /= CoordPos.w;
        if (CoordPos.x >= -1.0 && CoordPos.x <= 1.0 && CoordPos.y >= -1.0 && CoordPos.y <= 1.0 && CoordPos.z >= 0 && CoordPos.z <= 1)
        {
            float AtlasU = CoordPos.x * 0.5 + 0.5;
            AtlasU = (AtlasRect.x + AtlasU * AtlasRect.z) / AtlasWidth;
                    
            float AtlasV = -CoordPos.y * 0.5 + 0.5;
            AtlasV = (AtlasRect.y + AtlasV * AtlasRect.w) / AtlasHeight;
                    
            const float Bias = 0.00013;
            float SampledDepth = Atlas.SampleLevel(Sampler, float3(AtlasU, AtlasV, AtlasIndex), 0).r;
            if (SampledDepth < CoordPos.z - Bias)
            {
                Shadow = 0.0;
            }
        }
    }
    
    return Shadow;
}

#endif
