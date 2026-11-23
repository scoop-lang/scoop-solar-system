#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 vp, model, normal, physical;
  float4 sun, material, tint, eye, lightSun;
  float4 spheres[32];
  float4 ringCenter[2], ringNormal[2], ringSize[2];
  float4 options;
};
struct SurfaceVertex {
  float4 position [[position]];
  float3 world, normal, local;
  float2 uv;
};
struct PointVertex {
  float4 position [[position]];
  float pointSize [[point_size]];
  float4 color;
};
struct LineVertex {
  float4 position, color;
};
struct LineOutput {
  float4 position [[position]];
  float4 color;
};

float3 encode(float3 linear) {
  float3 tone = linear / (1.0 + linear);
  return select(1.055 * pow(tone, float3(1.0 / 2.4)) - 0.055, tone * 12.92,
                tone <= 0.0031308);
}
constexpr sampler mapSampler(coord::normalized, address::repeat, filter::linear,
                             mip_filter::linear);
constexpr sampler ringSampler(coord::normalized, address::clamp_to_edge,
                              filter::linear, mip_filter::linear);

vertex SurfaceVertex sphere_vertex(uint vid [[vertex_id]],
                                   constant Uniforms &u [[buffer(0)]]) {
  const uint corners[6] = {0, 1, 2, 2, 1, 3};
  uint corner = corners[vid % 6], quad = vid / 6;
  float2 uv = float2(float(quad % 96 + (corner & 1)) / 96.0,
                     float(quad / 96 + (corner >> 1)) / 48.0);
  float longitude = (uv.x - 0.5) * 2.0 * M_PI_F;
  float latitude = (0.5 - uv.y) * M_PI_F;
  float3 p = float3(cos(latitude) * cos(longitude),
                    cos(latitude) * sin(longitude), sin(latitude));
  float4 world = u.model * float4(p, 1);
  return {u.vp * world, world.xyz, normalize((u.normal * float4(p, 0)).xyz), p,
          uv};
}

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
  return clamp(1.0 - area / (M_PI_F * a), 0.0, 1.0);
}

float shadow(float3 p, float3 light, constant Uniforms &u, int own,
             texture2d<float> saturnRing, texture2d<float> uranusRing,
             bool testRings) {
  float distanceToSun = length(u.lightSun.xyz - p);
  float sunSine = u.lightSun.w / distanceToSun;
  float sunAngle = asin(clamp(sunSine, 0.0, 1.0));
  float transmission = 1.0;
  for (int i = 0; i < int(u.options.x); ++i) {
    if (i == own || u.spheres[i].w <= 0)
      continue;
    float3 offset = u.spheres[i].xyz - p;
    float along = dot(offset, light), distance = length(offset),
          radius = u.spheres[i].w;
    if (along <= 0 || distance >= distanceToSun)
      continue;
    float across = length(cross(offset, light));
    // A conservative cone bound avoids angular work for non-occulting bodies.
    if (across >= radius + distance * sunSine)
      continue;
    float bodyAngle = asin(clamp(radius / distance, 0.0, 1.0));
    float separation = atan2(across, along);
    transmission *= visibleSun(sunAngle, bodyAngle, separation);
    if (transmission <= 0.0)
      return 0.0;
  }
  if (testRings)
    for (int i = 0; i < 2; ++i) {
      if (u.ringSize[i].z < 0.5)
        continue;
      float denominator = dot(light, u.ringNormal[i].xyz);
      if (abs(denominator) < 0.0001)
        continue;
      float t = dot(u.ringCenter[i].xyz - p, u.ringNormal[i].xyz) / denominator;
      if (t <= 0.001 || t > distanceToSun)
        continue;
      float r = length(p + t * light - u.ringCenter[i].xyz);
      float inner = u.ringSize[i].x, outer = u.ringSize[i].y;
      if (r > inner && r < outer) {
        float2 uv = float2((r - inner) / (outer - inner), 0.5);
        float alpha = i == 0 ? saturnRing.sample(ringSampler, uv).a
                             : uranusRing.sample(ringSampler, uv).a;
        transmission *= 1.0 - alpha * 0.88;
      }
    }
  return transmission;
}

fragment float4 sphere_fragment(SurfaceVertex v [[stage_in]],
                                constant Uniforms &u [[buffer(0)]],
                                texture2d<float> colorMap [[texture(0)]],
                                texture2d<float> cloudMap [[texture(1)]],
                                texture2d<float> normalMap [[texture(2)]],
                                texture2d<float> saturnRing [[texture(3)]],
                                texture2d<float> uranusRing [[texture(4)]],
                                texture2d<float> nightMap [[texture(5)]]) {
  float3 albedo = colorMap.sample(mapSampler, v.uv).rgb;
  if (u.tint.z > 0.5)
    albedo = albedo.rrr;
  if (u.material.x == 10)
    return float4(encode(albedo * 3.6), 1);
  float3 n = normalize(v.normal);
  if (u.material.x == 399 || u.material.x == 301) {
    float longitude = (v.uv.x - 0.5) * 2.0 * M_PI_F;
    float3 tangent = normalize(
        (u.normal * float4(-sin(longitude), cos(longitude), 0, 0)).xyz);
    float3 bitangent = normalize(cross(n, tangent));
    float3 mapped = normalMap.sample(mapSampler, v.uv).xyz * 2.0 - 1.0;
    mapped.xy *= u.material.x == 301 ? 0.6 : 0.35;
    n = normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
  }
  float cloud = 0;
  if (u.material.x == 399) {
    float4 c = cloudMap.sample(mapSampler, v.uv);
    cloud = (u.options.w > 0.5 ? c.g : c.a) * c.r;
    albedo = mix(albedo, float3(0.86), cloud * 0.9);
  }
  float3 physical = (u.physical * float4(normalize(v.local), 0)).xyz;
  float3 l = normalize(u.lightSun.xyz - physical), eye = normalize(-v.world);
  float nl = max(dot(n, l), 0.0), nv = max(dot(n, eye), 0.001);
  float3 h = normalize(l + eye);
  float roughness = u.material.y;
  if (u.material.x == 399 && albedo.b > albedo.r * 1.3 && cloud < 0.15)
    roughness = 0.22;
  float a2 = pow(roughness, 4.0), nh = max(dot(n, h), 0.0);
  float d = a2 / max(M_PI_F * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0), 0.00001);
  float k = pow(roughness + 1.0, 2.0) / 8.0;
  float g = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
  float f = 0.04 + 0.96 * pow(1.0 - max(dot(h, eye), 0.0), 5.0);
  float specular = d * g * f / max(4.0 * nl * nv, 0.001);
  float visibility =
      shadow(physical, l, u, int(u.material.z), saturnRing, uranusRing, true);
  float illumination =
      1.45 /
      pow(max(length(u.lightSun.xyz - physical) / 149597870.7, 0.4), 0.09);
  float3 linear = albedo * 0.004 + (albedo * (1.0 - f) + specular) * nl *
                                       visibility * illumination;
  if (u.material.x == 399) {
    linear += nightMap.sample(mapSampler, v.uv).rgb * pow(1.0 - nl, 5.0) * 0.28;
    linear += float3(0.07, 0.22, 0.5) * pow(1.0 - nv, 3.0) *
              smoothstep(-0.2, 0.6, dot(n, l)) * 0.7 * visibility;
  }
  if (u.material.w > 0.5)
    linear += float3(0.12, 0.25, 0.35) * pow(1.0 - nv, 8.0);
  return float4(encode(linear), 1);
}

vertex SurfaceVertex ring_vertex(uint vid [[vertex_id]],
                                 constant Uniforms &u [[buffer(0)]]) {
  const uint corners[6] = {0, 1, 2, 2, 1, 3};
  uint c = corners[vid % 6];
  float angle = (float(vid / 6 + (c & 1)) / 384.0) * 2 * M_PI_F;
  float t = float(c >> 1), radius = mix(u.tint.x, u.tint.y, t);
  float3 local = float3(cos(angle) * radius, sin(angle) * radius, 0);
  float4 world = u.model * float4(local, 1);
  return {u.vp * world, world.xyz,
          normalize((u.normal * float4(0, 0, 1, 0)).xyz), local,
          float2(t, 0.5)};
}
fragment float4 ring_fragment(SurfaceVertex v [[stage_in]],
                              constant Uniforms &u [[buffer(0)]],
                              texture2d<float> map [[texture(0)]],
                              texture2d<float> saturnRing [[texture(3)]],
                              texture2d<float> uranusRing [[texture(4)]]) {
  float4 sample = map.sample(ringSampler, v.uv);
  float3 physical = (u.physical * float4(v.local, 0)).xyz;
  float3 l = normalize(u.lightSun.xyz - physical);
  float visibility = shadow(physical, l, u, -1, saturnRing, uranusRing, false);
  float nl = abs(dot(normalize(v.normal), l));
  return float4(encode(sample.rgb * (0.04 + (0.3 + nl) * visibility)),
                sample.a * 0.9);
}

vertex LineOutput line_vertex(uint vid [[vertex_id]],
                              const device LineVertex *vertices [[buffer(1)]],
                              constant Uniforms &u [[buffer(0)]]) {
  return {u.vp * vertices[vid].position, vertices[vid].color};
}
fragment float4 line_fragment(LineOutput v [[stage_in]]) { return v.color; }

float hash(uint n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n ^= n >> 4;
  n *= 0x27d4eb2du;
  n ^= n >> 15;
  return float(n) / 4294967296.0;
}
vertex PointVertex particle_vertex(uint vid [[vertex_id]],
                                   constant Uniforms &u [[buffer(0)]]) {
  float3 p;
  float size;
  float4 color;
  if (u.tint.w < 0.5) {
    float z = hash(vid * 5 + 1) * 2 - 1, angle = hash(vid * 5 + 2) * 2 * M_PI_F;
    float r = sqrt(1 - z * z);
    p = float3(r * cos(angle), r * sin(angle), z) * 900;
    size = 1.0 + hash(vid * 5 + 3) * 1.8;
    color = float4(0.52, 0.65, 0.85, 0.15 + hash(vid * 5 + 4) * 0.65);
  } else {
    float a = 2.12 + hash(vid * 7 + 1) * 1.18, e = hash(vid * 7 + 2) * 0.13;
    float m = hash(vid * 7 + 3) * 2 * M_PI_F +
              u.options.y * 2 * M_PI_F / (365.25 * pow(a, 1.5));
    m = fmod(m, 2 * M_PI_F);
    float anomaly = m;
    for (int i = 0; i < 5; i++)
      anomaly -= (anomaly - e * sin(anomaly) - m) / (1 - e * cos(anomaly));
    float3 local =
        float3(a * (cos(anomaly) - e), a * sqrt(1 - e * e) * sin(anomaly), 0);
    float node = hash(vid * 7 + 4) * 2 * M_PI_F,
          inclination = (hash(vid * 7 + 5) - 0.5) * 0.18;
    local.yz = float2(local.y * cos(inclination), local.y * sin(inclination));
    p = float3(local.x * cos(node) - local.y * sin(node),
               local.x * sin(node) + local.y * cos(node), local.z) -
        u.eye.xyz;
    size = 1.3 + hash(vid * 7 + 6) * 1.1;
    color = float4(0.56, 0.50, 0.40, 0.32);
  }
  return {u.vp * float4(p, 1), size, color};
}
fragment float4 particle_fragment(PointVertex v [[stage_in]],
                                  float2 point [[point_coord]]) {
  float r = length(point - 0.5) * 2;
  if (r > 1)
    discard_fragment();
  return float4(v.color.rgb, v.color.a * (1 - r * r));
}

vertex PointVertex glow_vertex(uint vid [[vertex_id]],
                               constant Uniforms &u [[buffer(0)]]) {
  (void)vid;
  return {u.vp * float4(u.sun.xyz, 1), u.tint.x, float4(1.0, 0.7, 0.35, 0.26)};
}
fragment float4 glow_fragment(PointVertex v [[stage_in]],
                              float2 point [[point_coord]]) {
  float r = length(point - 0.5) * 2;
  float alpha = exp(-r * r * 6.0) * smoothstep(1.0, 0.6, r);
  return float4(v.color.rgb, alpha * v.color.a);
}
