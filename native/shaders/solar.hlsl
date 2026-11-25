// HLSL (Shader Model 5.1) port of solar.glsl / solar.metal. The adapter
// compiles one stage define (VERTEX or FRAGMENT) plus one program define
// (SPHERE, RING, LINE, PARTICLE or GLOW). D3D12 has no point sprites, so the
// particle and glow programs expand each point into a screen-space quad.

#define PI 3.14159265358979

// Column-major matrices; the layout matches the std140 block of the GLES port.
cbuffer Uniforms : register(b0) {
  column_major float4x4 vp, model, normalMatrix, physicalMatrix;
  float4 sun, material, tint, eye, lightSun;
  float4 spheres[32];
  float4 ringCenter[2], ringNormal[2], ringSize[2];
  // options.z is the target height; viewport.xy is the target size in pixels.
  float4 options, viewport;
};

struct SurfaceVertex {
  float4 position : SV_Position;
  float3 world : WORLD;
  float3 normal : NORMAL;
  float3 local : LOCAL;
  float2 uv : TEXCOORD0;
};
struct PointVertex {
  float4 position : SV_Position;
  float4 color : COLOR;
  float2 sprite : TEXCOORD0;
};
struct LineVertex {
  float4 position : SV_Position;
  float4 color : COLOR;
};

static const uint corners[6] = {0, 1, 2, 2, 1, 3};

#ifdef VERTEX

#ifdef SPHERE
SurfaceVertex main(uint vid : SV_VertexID) {
  uint corner = corners[vid % 6u], quad = vid / 6u;
  float2 uv = float2(float(quad % 96u + (corner & 1u)) / 96.0,
                     float(quad / 96u + (corner >> 1u)) / 48.0);
  float longitude = (uv.x - 0.5) * 2.0 * PI;
  float latitude = (0.5 - uv.y) * PI;
  float3 p = float3(cos(latitude) * cos(longitude),
                    cos(latitude) * sin(longitude), sin(latitude));
  float4 world = mul(model, float4(p, 1));
  SurfaceVertex output;
  output.position = mul(vp, world);
  output.world = world.xyz;
  output.normal = normalize(mul(normalMatrix, float4(p, 0)).xyz);
  output.local = p;
  output.uv = uv;
  return output;
}
#endif

#ifdef RING
SurfaceVertex main(uint vid : SV_VertexID) {
  uint c = corners[vid % 6u];
  float angle = (float(vid / 6u + (c & 1u)) / 384.0) * 2.0 * PI;
  float t = float(c >> 1u), radius = lerp(tint.x, tint.y, t);
  float3 local = float3(cos(angle) * radius, sin(angle) * radius, 0);
  float4 world = mul(model, float4(local, 1));
  SurfaceVertex output;
  output.position = mul(vp, world);
  output.world = world.xyz;
  output.normal = normalize(mul(normalMatrix, float4(0, 0, 1, 0)).xyz);
  output.local = local;
  output.uv = float2(t, 0.5);
  return output;
}
#endif

#ifdef LINE
LineVertex main(float4 linePosition : POSITION, float4 lineColor : COLOR) {
  LineVertex output;
  output.position = mul(vp, linePosition);
  output.color = lineColor;
  return output;
}
#endif

#if defined(PARTICLE) || defined(GLOW)
// Expands a point of the given pixel size like a GL/Metal point sprite;
// sprite uses the same top-left origin as gl_PointCoord.
PointVertex expand(float4 center, float size, float4 color, uint vid) {
  uint corner = corners[vid % 6u];
  float2 sprite = float2(float(corner & 1u), float(corner >> 1u));
  float2 offset = (sprite - 0.5) * float2(1, -1) * size / viewport.xy * 2.0;
  PointVertex output;
  output.position = center + float4(offset * center.w, 0, 0);
  output.color = color;
  output.sprite = sprite;
  return output;
}
#endif

#ifdef PARTICLE
float hash(uint n) {
  n = (n ^ 61u) ^ (n >> 16u);
  n *= 9u;
  n ^= n >> 4u;
  n *= 0x27d4eb2du;
  n ^= n >> 15u;
  return float(n) / 4294967296.0;
}
PointVertex main(uint id : SV_VertexID) {
  uint vid = id / 6u;
  float3 p;
  float size;
  float4 color;
  if (tint.w < 0.5) {
    float z = hash(vid * 5u + 1u) * 2.0 - 1.0,
          angle = hash(vid * 5u + 2u) * 2.0 * PI;
    float r = sqrt(1.0 - z * z);
    p = float3(r * cos(angle), r * sin(angle), z) * 900.0;
    size = 1.0 + hash(vid * 5u + 3u) * 1.8;
    color = float4(0.52, 0.65, 0.85, 0.15 + hash(vid * 5u + 4u) * 0.65);
  } else {
    float a = 2.12 + hash(vid * 7u + 1u) * 1.18,
          e = hash(vid * 7u + 2u) * 0.13;
    float m = hash(vid * 7u + 3u) * 2.0 * PI +
              options.y * 2.0 * PI / (365.25 * pow(a, 1.5));
    m = m - 2.0 * PI * trunc(m / (2.0 * PI));
    float anomaly = m;
    for (int i = 0; i < 5; i++)
      anomaly -= (anomaly - e * sin(anomaly) - m) / (1.0 - e * cos(anomaly));
    float3 local = float3(a * (cos(anomaly) - e),
                          a * sqrt(1.0 - e * e) * sin(anomaly), 0);
    float node = hash(vid * 7u + 4u) * 2.0 * PI,
          inclination = (hash(vid * 7u + 5u) - 0.5) * 0.18;
    local.yz = float2(local.y * cos(inclination), local.y * sin(inclination));
    p = float3(local.x * cos(node) - local.y * sin(node),
               local.x * sin(node) + local.y * cos(node), local.z) -
        eye.xyz;
    size = 1.3 + hash(vid * 7u + 6u) * 1.1;
    color = float4(0.56, 0.50, 0.40, 0.32);
  }
  return expand(mul(vp, float4(p, 1)), size, color, id);
}
#endif

#ifdef GLOW
PointVertex main(uint id : SV_VertexID) {
  return expand(mul(vp, float4(sun.xyz, 1)), tint.x,
                float4(1.0, 0.7, 0.35, 0.26), id);
}
#endif

#endif // VERTEX

#ifdef FRAGMENT

float3 encode(float3 lit) {
  float3 tone = lit / (1.0 + lit);
  return lerp(1.055 * pow(tone, 1.0 / 2.4) - 0.055, tone * 12.92,
              (float3)(tone <= 0.0031308));
}

#if defined(SPHERE) || defined(RING)
// Registers t0..t5 follow the Metal texture indices; t3 and t4 always hold the
// Saturn and Uranus ring maps used by the shadow test.
Texture2D colorMap : register(t0);
Texture2D cloudMap : register(t1);
Texture2D normalMap : register(t2);
Texture2D saturnRing : register(t3);
Texture2D uranusRing : register(t4);
Texture2D nightMap : register(t5);
SamplerState mapSampler : register(s0);
SamplerState ringSampler : register(s1);

float visibleSun(float sunRadius, float bodyRadius, float separation) {
  // Apparent discs on the local angular plane; keep umbra and annular limits
  // exact.
  if (separation >= sunRadius + bodyRadius)
    return 1.0;
  if (separation <= abs(sunRadius - bodyRadius))
    return 1.0 - min(1.0, (bodyRadius * bodyRadius) / (sunRadius * sunRadius));
  float a = sunRadius * sunRadius, b = bodyRadius * bodyRadius;
  float d = separation * separation;
  float x = clamp((d + a - b) / (2.0 * separation * sunRadius), -1.0, 1.0);
  float y = clamp((d + b - a) / (2.0 * separation * bodyRadius), -1.0, 1.0);
  float area = a * acos(x) + b * acos(y) -
               0.5 * sqrt(max((-separation + sunRadius + bodyRadius) *
                                  (separation + sunRadius - bodyRadius) *
                                  (separation - sunRadius + bodyRadius) *
                                  (separation + sunRadius + bodyRadius),
                              0.0));
  return clamp(1.0 - area / (PI * a), 0.0, 1.0);
}

float shadow(float3 p, float3 light, int own, bool testRings) {
  float distanceToSun = length(lightSun.xyz - p);
  float sunSine = lightSun.w / distanceToSun;
  float sunAngle = asin(clamp(sunSine, 0.0, 1.0));
  float transmission = 1.0;
  for (int i = 0; i < int(options.x); ++i) {
    if (i == own || spheres[i].w <= 0.0)
      continue;
    float3 offset = spheres[i].xyz - p;
    float along = dot(offset, light), separationDistance = length(offset),
          radius = spheres[i].w;
    if (along <= 0.0 || separationDistance >= distanceToSun)
      continue;
    float across = length(cross(offset, light));
    // A conservative cone bound avoids angular work for non-occulting bodies.
    if (across >= radius + separationDistance * sunSine)
      continue;
    float bodyAngle = asin(clamp(radius / separationDistance, 0.0, 1.0));
    float separation = atan2(across, along);
    transmission *= visibleSun(sunAngle, bodyAngle, separation);
    if (transmission <= 0.0)
      return 0.0;
  }
  if (testRings)
    for (int i = 0; i < 2; ++i) {
      if (ringSize[i].z < 0.5)
        continue;
      float denominator = dot(light, ringNormal[i].xyz);
      if (abs(denominator) < 0.0001)
        continue;
      float t = dot(ringCenter[i].xyz - p, ringNormal[i].xyz) / denominator;
      if (t <= 0.001 || t > distanceToSun)
        continue;
      float r = length(p + t * light - ringCenter[i].xyz);
      float inner = ringSize[i].x, outer = ringSize[i].y;
      if (r > inner && r < outer) {
        // Explicit LOD: implicit derivatives are unavailable in this loop.
        float2 uv = float2((r - inner) / (outer - inner), 0.5);
        float alpha = i == 0 ? saturnRing.SampleLevel(ringSampler, uv, 0).a
                             : uranusRing.SampleLevel(ringSampler, uv, 0).a;
        transmission *= 1.0 - alpha * 0.88;
      }
    }
  return transmission;
}
#endif

#ifdef SPHERE
float4 main(SurfaceVertex input) : SV_Target {
  float4 color = colorMap.Sample(mapSampler, input.uv);
  float4 c = cloudMap.Sample(mapSampler, input.uv);
  float3 mapped = normalMap.Sample(mapSampler, input.uv).xyz * 2.0 - 1.0;
  float3 night = nightMap.Sample(mapSampler, input.uv).rgb;
  float3 albedo = color.rgb;
  if (tint.z > 0.5)
    albedo = albedo.rrr;
  if (material.x == 10.0)
    return float4(encode(albedo * 3.6), 1);
  float3 n = normalize(input.normal);
  if (material.x == 399.0 || material.x == 301.0) {
    float longitude = (input.uv.x - 0.5) * 2.0 * PI;
    float3 tangent = normalize(
        mul(normalMatrix, float4(-sin(longitude), cos(longitude), 0, 0)).xyz);
    float3 bitangent = normalize(cross(n, tangent));
    mapped.xy *= material.x == 301.0 ? 0.6 : 0.35;
    n = normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
  }
  float cloud = 0.0;
  if (material.x == 399.0) {
    cloud = (options.w > 0.5 ? c.g : c.a) * c.r;
    albedo = lerp(albedo, (float3)0.86, cloud * 0.9);
  }
  float3 physical = mul(physicalMatrix, float4(normalize(input.local), 0)).xyz;
  float3 l = normalize(lightSun.xyz - physical),
         eyeDirection = normalize(-input.world);
  float nl = max(dot(n, l), 0.0), nv = max(dot(n, eyeDirection), 0.001);
  float3 h = normalize(l + eyeDirection);
  float roughness = material.y;
  if (material.x == 399.0 && albedo.b > albedo.r * 1.3 && cloud < 0.15)
    roughness = 0.22;
  float a2 = pow(roughness, 4.0), nh = max(dot(n, h), 0.0);
  float d = a2 / max(PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0), 0.00001);
  float k = pow(roughness + 1.0, 2.0) / 8.0;
  float g = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
  float f = 0.04 + 0.96 * pow(1.0 - max(dot(h, eyeDirection), 0.0), 5.0);
  float specular = d * g * f / max(4.0 * nl * nv, 0.001);
  float visibility = shadow(physical, l, int(material.z), true);
  float illumination =
      1.45 / pow(max(length(lightSun.xyz - physical) / 149597870.7, 0.4), 0.09);
  float3 lit = albedo * 0.004 + (albedo * (1.0 - f) + specular) * nl *
                                       visibility * illumination;
  if (material.x == 399.0) {
    lit += night * pow(1.0 - nl, 5.0) * 0.28;
    lit += float3(0.07, 0.22, 0.5) * pow(1.0 - nv, 3.0) *
              smoothstep(-0.2, 0.6, dot(n, l)) * 0.7 * visibility;
  }
  if (material.w > 0.5)
    lit += float3(0.12, 0.25, 0.35) * pow(1.0 - nv, 8.0);
  return float4(encode(lit), 1);
}
#endif

#ifdef RING
float4 main(SurfaceVertex input) : SV_Target {
  float4 sampled = colorMap.Sample(ringSampler, input.uv);
  float3 physical = mul(physicalMatrix, float4(input.local, 0)).xyz;
  float3 l = normalize(lightSun.xyz - physical);
  float visibility = shadow(physical, l, -1, false);
  float nl = abs(dot(normalize(input.normal), l));
  return float4(encode(sampled.rgb * (0.04 + (0.3 + nl) * visibility)),
                sampled.a * 0.9);
}
#endif

#ifdef LINE
float4 main(LineVertex input) : SV_Target { return input.color; }
#endif

#ifdef PARTICLE
float4 main(PointVertex input) : SV_Target {
  float r = length(input.sprite - 0.5) * 2.0;
  if (r > 1.0)
    discard;
  return float4(input.color.rgb, input.color.a * (1.0 - r * r));
}
#endif

#ifdef GLOW
float4 main(PointVertex input) : SV_Target {
  float r = length(input.sprite - 0.5) * 2.0;
  float alpha = exp(-r * r * 6.0) * smoothstep(1.0, 0.6, r);
  return float4(input.color.rgb, alpha * input.color.a);
}
#endif

#endif // FRAGMENT
