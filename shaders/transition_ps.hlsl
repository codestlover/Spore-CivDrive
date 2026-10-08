
sampler2D src : register(s0);
float4 p0 : register(c0);
float4 p1 : register(c1);
float4 p2 : register(c2);

#ifndef TAPS
#define TAPS 10
#endif

float hash(float n) { return frac(sin(n) * 43758.5453); }

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float s = p0.x;
    float2 c = p1.zw;
    float2 d = uv - c;
    float2 da = d * float2(p0.w, 1.0);
    float r = length(da);

    float zoom = s * 0.20 * p0.z;
    float4 acc = 0;
    float wsum = 0;
    for (int i = 0; i < TAPS; ++i) {
        float k = i / (float)(TAPS - 1);
        float sc = 1.0 - zoom * k * (0.35 + r);
        float w = 1.0 - k * 0.65;
        acc += tex2D(src, c + d * sc) * w;
        wsum += w;
    }
    float4 col = acc / wsum;

    float ca = s * 0.010 * (0.25 + r * 1.5);
    float red = tex2D(src, c + d * (1.0 + ca)).r;
    float blue = tex2D(src, c + d * (1.0 - ca)).b;
    col.r = lerp(col.r, red, saturate(s * 1.5));
    col.b = lerp(col.b, blue, saturate(s * 1.5));

#if TAPS > 6
    float ang = atan2(da.y, da.x);
    float lane = floor(ang * 57.0);
    float flick = floor(p0.y * 24.0);
    float streak = step(0.86, hash(lane * 1.37 + flick * 7.13)) * smoothstep(0.28, 0.85, r);
    col.rgb += streak * s * 0.22 * float3(0.85, 0.93, 1.0);
#endif

    col.rgb = lerp(col.rgb, col.rgb * float3(0.86, 0.96, 1.14), s * 0.55);
    col.rgb += s * s * s * 0.10;

    float vig = max(s * 0.60, p2.x);
    col.rgb *= 1.0 - vig * smoothstep(0.30, 1.05, r);

    col.a = 1.0;
    return col;
}

float4 vignette(float2 uv : TEXCOORD0) : COLOR
{
    float2 da = (uv - p1.zw) * float2(p0.w, 1.0);
    float r = length(da);
    return float4(0, 0, 0, p2.x * smoothstep(0.30, 1.05, r));
}

