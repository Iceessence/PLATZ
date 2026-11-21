#include "color.slangh"

[[vk::binding(0, 0)]] cbuffer Scene {
    row_major float4x4 viewProjection;
    row_major float4x4 world;
    row_major float4x4 normalMatrix;
    float3 baseColor;
    uint token;
    float3 eye;
    uint selected;
    uint meshKind;
};

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};
struct Surface {
    float4 clip : SV_Position;
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 local : TEXCOORD;
};
struct Pixel {
    float4 color : SV_Target0;
    uint objectId : SV_Target1;
};

[shader("vertex")]
Surface vsMain(Vertex v) {
    Surface result;
    float4 p = mul(float4(v.position, 1), world);
    result.position = p.xyz;
    result.local = v.position;
    result.normal = mul(float4(v.normal, 0), normalMatrix).xyz;
    result.clip = mul(p, viewProjection);
    return result;
}

float gridLine(float2 pos) {
    float2 width = max(fwidth(pos), 0.0001);
    float2 edge = abs(frac(pos - 0.5) - 0.5) / width;
    float coverage = 1.0 - saturate(min(edge.x, edge.y));
    return coverage * (1.0 - smoothstep(0.25, 0.8, max(width.x, width.y)));
}

[shader("fragment")]
Pixel psMain(Surface s) {
    Pixel result;
    result.objectId = token;
    if (token == 0) {
        float2 xz = s.position.xz;
        float distanceToEye = length(eye - s.position);
        float fade = 1.0 - smoothstep(14, 70, distanceToEye);
        float fine = gridLine(xz * 2.0) * 0.042;
        float major = gridLine(xz / 2.0) * 0.077;
        float3 ground = float3(0.131, 0.154, 0.18);
        float3 background = float3(0.100, 0.126, 0.155);
        float3 color = lerp(background, ground + fine + major, fade);
        float2 axis = 1 - saturate(abs(xz) / max(fwidth(xz), 0.0001));
        color = lerp(color, float3(0.39, 0.26, 0.25), axis.y * fade * 0.65);
        color = lerp(color, float3(0.21, 0.35, 0.42), axis.x * fade * 0.65);
        result.color = float4(color, 1);
        return result;
    }
    float3 n = normalize(s.normal);
    float3 light = normalize(float3(-3, 6, 3));
    float3 view = normalize(eye - s.position);
    float diffuse = saturate(dot(light, n));
    float hemi = saturate(n.y * 0.5 + 0.5);
    float3 irradiance = lerp(float3(0.22, 0.23, 0.28), float3(0.46, 0.54, 0.65), hemi)
                      + diffuse * float3(0.85, 0.80, 0.72);
    float spec = pow(saturate(dot(n, normalize(view + light))), 36) * diffuse * 0.12;
    float3 color = linearToSrgb(srgbToLinear(baseColor) * irradiance + spec);
    if (token == selected) {
        float outline = 1 - smoothstep(0.06, 0.24, abs(dot(n, view)));
        if (meshKind == 0) {
            float3 q = abs(s.local);
            float middle = max(min(q.x, q.y), min(max(q.x, q.y), q.z));
            outline = 1 - smoothstep(0.0, max(fwidth(middle) * 1.6, 0.001), 0.5 - middle);
        }
        color = lerp(color, float3(1.0, 0.72, 0.39), saturate(outline) * 0.95);
    }
    result.color = float4(color, 1);
    return result;
}
