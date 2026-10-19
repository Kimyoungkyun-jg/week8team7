#pragma pack_matrix(row_major)

// 화면 픽셀마다 Shadow Map과 비교해 Mask(1 = 밝음, 0 = 그림자)를 만든다.
// 언리얼 ShadowProjectionPixelShader.usf에 해당.
Texture2D<float> SceneDepth : register(t0);
Texture2D<float> ShadowDepth : register(t1);
SamplerComparisonState ShadowSampler : register(s0);

cbuffer ShadowProjection : register(b0)
{
    matrix InverseViewProjection;
    matrix ShadowViewProjection;
    float4 ShadowUVScaleOffset; // xy: 배율, zw: 오프셋
    float4 ShadowParams; // x: 텍셀 크기, y: 비교 bias
    float4 ViewportSize;
};

struct FPSInput
{
    float4 Position : SV_POSITION;
};

// ToneMapShader와 같은 화면 삼각형
FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float Depth = SceneDepth.Load(int3(Pixel, 0));
    if (Depth >= 1.0f)
        return 1.0f; // 하늘

    // DeferredLightingShader와 같은 위치 복원
    float2 UV = (float2(Pixel) + 0.5f) / ViewportSize.xy;
    float2 NDC = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
    float4 WorldH = mul(float4(NDC, Depth, 1.0f), InverseViewProjection);
    if (abs(WorldH.w) < 1.0e-6f)
        return 1.0f;
    float3 World = WorldH.xyz / WorldH.w;

    // 그림자 카메라 공간으로
    float4 S = mul(float4(World, 1.0f), ShadowViewProjection);
    if (S.w <= 0.0f)
        return 1.0f; // 라이트 뒤쪽
    S.xyz /= S.w;
    float2 ShadowUV = float2(S.x * 0.5f + 0.5f, 0.5f - S.y * 0.5f);
    if (any(ShadowUV < 0.0f) || any(ShadowUV > 1.0f) || S.z > 1.0f)
        return 1.0f;
    ShadowUV = ShadowUV * ShadowUVScaleOffset.xy + ShadowUVScaleOffset.zw;

    // 3x3 PCF
    float Sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
        for (int x = -1; x <= 1; ++x)
            Sum += ShadowDepth.SampleCmpLevelZero(ShadowSampler,
               ShadowUV + float2(x, y) * ShadowParams.x, S.z - ShadowParams.y);

    return (Sum / 9.0f).xxxx;  // R 채널 = 채널 0
}