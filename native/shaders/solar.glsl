// GLSL ES 3.00 port of solar.metal. The adapter prepends the version line and
// one stage define (VERTEX or FRAGMENT) plus one program define (SPHERE, RING,
// LINE, PARTICLE or GLOW).
precision highp float;
precision highp int;

#define PI 3.14159265358979

layout(std140) uniform Uniforms {
  mat4 vp, model, normalMatrix, physicalMatrix;
  vec4 sun, material, tint, eye, lightSun;
  vec4 spheres[32];
  vec4 ringCenter[2], ringNormal[2], ringSize[2];
  vec4 options;
}
u;

#ifdef VERTEX
#define VARYING out
#else
#define VARYING in
#endif

#if defined(SPHERE) || defined(RING)
VARYING vec3 vWorld;
VARYING vec3 vNormal;
VARYING vec3 vLocal;
VARYING vec2 vUv;
#else
VARYING vec4 vColor;
#endif

#ifdef VERTEX

const uint corners[6] = uint[6](0u, 1u, 2u, 2u, 1u, 3u);

#ifdef SPHERE
void main() {
  uint vid = uint(gl_VertexID);
  uint corner = corners[vid % 6u], quad = vid / 6u;
  vec2 uv = vec2(float(quad % 96u + (corner & 1u)) / 96.0,
                 float(quad / 96u + (corner >> 1u)) / 48.0);
  float longitude = (uv.x - 0.5) * 2.0 * PI;
  float latitude = (0.5 - uv.y) * PI;
  vec3 p = vec3(cos(latitude) * cos(longitude), cos(latitude) * sin(longitude),
                sin(latitude));
  vec4 world = u.model * vec4(p, 1);
  gl_Position = u.vp * world;
  vWorld = world.xyz;
  vNormal = normalize((u.normalMatrix * vec4(p, 0)).xyz);
  vLocal = p;
  vUv = uv;
}
#endif

#ifdef RING
void main() {
  uint vid = uint(gl_VertexID);
  uint c = corners[vid % 6u];
  float angle = (float(vid / 6u + (c & 1u)) / 384.0) * 2.0 * PI;
  float t = float(c >> 1u), radius = mix(u.tint.x, u.tint.y, t);
  vec3 local = vec3(cos(angle) * radius, sin(angle) * radius, 0);
  vec4 world = u.model * vec4(local, 1);
  gl_Position = u.vp * world;
  vWorld = world.xyz;
  vNormal = normalize((u.normalMatrix * vec4(0, 0, 1, 0)).xyz);
  vLocal = local;
  vUv = vec2(t, 0.5);
}
#endif

#ifdef LINE
layout(location = 0) in vec4 linePosition;
layout(location = 1) in vec4 lineColor;
void main() {
  gl_Position = u.vp * linePosition;
  vColor = lineColor;
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
void main() {
  uint vid = uint(gl_VertexID);
  vec3 p;
  float size;
  vec4 color;
  if (u.tint.w < 0.5) {
    float z = hash(vid * 5u + 1u) * 2.0 - 1.0,
          angle = hash(vid * 5u + 2u) * 2.0 * PI;
    float r = sqrt(1.0 - z * z);
    p = vec3(r * cos(angle), r * sin(angle), z) * 900.0;
    size = 1.0 + hash(vid * 5u + 3u) * 1.8;
    color = vec4(0.52, 0.65, 0.85, 0.15 + hash(vid * 5u + 4u) * 0.65);
  } else {
    float a = 2.12 + hash(vid * 7u + 1u) * 1.18,
          e = hash(vid * 7u + 2u) * 0.13;
    float m = hash(vid * 7u + 3u) * 2.0 * PI +
              u.options.y * 2.0 * PI / (365.25 * pow(a, 1.5));
    m = m - 2.0 * PI * trunc(m / (2.0 * PI));
    float anomaly = m;
    for (int i = 0; i < 5; i++)
      anomaly -=
          (anomaly - e * sin(anomaly) - m) / (1.0 - e * cos(anomaly));
    vec3 local =
        vec3(a * (cos(anomaly) - e), a * sqrt(1.0 - e * e) * sin(anomaly), 0);
    float node = hash(vid * 7u + 4u) * 2.0 * PI,
          inclination = (hash(vid * 7u + 5u) - 0.5) * 0.18;
    local.yz = vec2(local.y * cos(inclination), local.y * sin(inclination));
    p = vec3(local.x * cos(node) - local.y * sin(node),
             local.x * sin(node) + local.y * cos(node), local.z) -
        u.eye.xyz;
    size = 1.3 + hash(vid * 7u + 6u) * 1.1;
    color = vec4(0.56, 0.50, 0.40, 0.32);
  }
  gl_Position = u.vp * vec4(p, 1);
  gl_PointSize = size;
  vColor = color;
}
#endif

#ifdef GLOW
void main() {
  gl_Position = u.vp * vec4(u.sun.xyz, 1);
  gl_PointSize = u.tint.x;
  vColor = vec4(1.0, 0.7, 0.35, 0.26);
}
#endif

#endif // VERTEX

#ifdef FRAGMENT
out vec4 fragColor;

vec3 encode(vec3 linear) {
  vec3 tone = linear / (1.0 + linear);
  return mix(1.055 * pow(tone, vec3(1.0 / 2.4)) - 0.055, tone * 12.92,
             lessThanEqual(tone, vec3(0.0031308)));
}

#if defined(SPHERE) || defined(RING)
// Units 0..5 follow the Metal texture indices; units 3 and 4 always hold the
// Saturn and Uranus ring maps used by the shadow test.
uniform sampler2D colorMap;
uniform sampler2D cloudMap;
uniform sampler2D normalMap;
uniform sampler2D saturnRing;
uniform sampler2D uranusRing;
uniform sampler2D nightMap;

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

float shadow(vec3 p, vec3 light, int own, bool testRings) {
  float distanceToSun = length(u.lightSun.xyz - p);
  float sunSine = u.lightSun.w / distanceToSun;
  float sunAngle = asin(clamp(sunSine, 0.0, 1.0));
  float transmission = 1.0;
  for (int i = 0; i < int(u.options.x); ++i) {
    if (i == own || u.spheres[i].w <= 0.0)
      continue;
    vec3 offset = u.spheres[i].xyz - p;
    float along = dot(offset, light), separationDistance = length(offset),
          radius = u.spheres[i].w;
    if (along <= 0.0 || separationDistance >= distanceToSun)
      continue;
    float across = length(cross(offset, light));
    // A conservative cone bound avoids angular work for non-occulting bodies.
    if (across >= radius + separationDistance * sunSine)
      continue;
    float bodyAngle = asin(clamp(radius / separationDistance, 0.0, 1.0));
    float separation = atan(across, along);
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
        vec2 uv = vec2((r - inner) / (outer - inner), 0.5);
        float alpha = i == 0 ? texture(saturnRing, uv).a
                             : texture(uranusRing, uv).a;
        transmission *= 1.0 - alpha * 0.88;
      }
    }
  return transmission;
}
#endif

#ifdef SPHERE
void main() {
  vec3 albedo = texture(colorMap, vUv).rgb;
  if (u.tint.z > 0.5)
    albedo = albedo.rrr;
  if (u.material.x == 10.0) {
    fragColor = vec4(encode(albedo * 3.6), 1);
    return;
  }
  vec3 n = normalize(vNormal);
  if (u.material.x == 399.0 || u.material.x == 301.0) {
    float longitude = (vUv.x - 0.5) * 2.0 * PI;
    vec3 tangent = normalize(
        (u.normalMatrix * vec4(-sin(longitude), cos(longitude), 0, 0)).xyz);
    vec3 bitangent = normalize(cross(n, tangent));
    vec3 mapped = texture(normalMap, vUv).xyz * 2.0 - 1.0;
    mapped.xy *= u.material.x == 301.0 ? 0.6 : 0.35;
    n = normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
  }
  float cloud = 0.0;
  if (u.material.x == 399.0) {
    vec4 c = texture(cloudMap, vUv);
    cloud = (u.options.w > 0.5 ? c.g : c.a) * c.r;
    albedo = mix(albedo, vec3(0.86), cloud * 0.9);
  }
  vec3 physical = (u.physicalMatrix * vec4(normalize(vLocal), 0)).xyz;
  vec3 l = normalize(u.lightSun.xyz - physical), eye = normalize(-vWorld);
  float nl = max(dot(n, l), 0.0), nv = max(dot(n, eye), 0.001);
  vec3 h = normalize(l + eye);
  float roughness = u.material.y;
  if (u.material.x == 399.0 && albedo.b > albedo.r * 1.3 && cloud < 0.15)
    roughness = 0.22;
  float a2 = pow(roughness, 4.0), nh = max(dot(n, h), 0.0);
  float d = a2 / max(PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0), 0.00001);
  float k = pow(roughness + 1.0, 2.0) / 8.0;
  float g = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
  float f = 0.04 + 0.96 * pow(1.0 - max(dot(h, eye), 0.0), 5.0);
  float specular = d * g * f / max(4.0 * nl * nv, 0.001);
  float visibility = shadow(physical, l, int(u.material.z), true);
  float illumination =
      1.45 /
      pow(max(length(u.lightSun.xyz - physical) / 149597870.7, 0.4), 0.09);
  vec3 linear = albedo * 0.004 + (albedo * (1.0 - f) + specular) * nl *
                                     visibility * illumination;
  if (u.material.x == 399.0) {
    linear += texture(nightMap, vUv).rgb * pow(1.0 - nl, 5.0) * 0.28;
    linear += vec3(0.07, 0.22, 0.5) * pow(1.0 - nv, 3.0) *
              smoothstep(-0.2, 0.6, dot(n, l)) * 0.7 * visibility;
  }
  if (u.material.w > 0.5)
    linear += vec3(0.12, 0.25, 0.35) * pow(1.0 - nv, 8.0);
  fragColor = vec4(encode(linear), 1);
}
#endif

#ifdef RING
void main() {
  vec4 sampled = texture(colorMap, vUv);
  vec3 physical = (u.physicalMatrix * vec4(vLocal, 0)).xyz;
  vec3 l = normalize(u.lightSun.xyz - physical);
  float visibility = shadow(physical, l, -1, false);
  float nl = abs(dot(normalize(vNormal), l));
  fragColor = vec4(encode(sampled.rgb * (0.04 + (0.3 + nl) * visibility)),
                   sampled.a * 0.9);
}
#endif

#ifdef LINE
void main() { fragColor = vColor; }
#endif

#ifdef PARTICLE
void main() {
  float r = length(gl_PointCoord - 0.5) * 2.0;
  if (r > 1.0)
    discard;
  fragColor = vec4(vColor.rgb, vColor.a * (1.0 - r * r));
}
#endif

#ifdef GLOW
void main() {
  float r = length(gl_PointCoord - 0.5) * 2.0;
  float alpha = exp(-r * r * 6.0) * smoothstep(1.0, 0.6, r);
  fragColor = vec4(vColor.rgb, alpha * vColor.a);
}
#endif

#endif // FRAGMENT
