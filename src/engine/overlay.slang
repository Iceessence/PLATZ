struct Vertex {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
    [[vk::location(2)]] float2 depthOpacity : TEXCOORD0;
};

struct Varyings {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
};

struct OverlayStyle { float opacity; };
[[vk::push_constant]] ConstantBuffer<OverlayStyle> overlayStyle;

[shader("vertex")]
Varyings overlayVS(Vertex input) {
    Varyings result;
    result.position = float4(input.position, input.depthOpacity.x, 1);
    result.color = float4(input.color, input.depthOpacity.y);
    return result;
}

[shader("fragment")]
float4 overlayPS(Varyings input) : SV_Target0 {
    return float4(input.color.rgb, input.color.a * overlayStyle.opacity);
}
