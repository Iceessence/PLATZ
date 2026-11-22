struct Vertex {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct Varyings {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
};

[shader("vertex")]
Varyings overlayVS(Vertex input) {
    Varyings result;
    result.position = float4(input.position, 0, 1);
    result.color = input.color;
    return result;
}

[shader("fragment")]
float4 overlayPS(Varyings input) : SV_Target0 {
    return float4(input.color, 1);
}
