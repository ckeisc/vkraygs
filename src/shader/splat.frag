#version 460

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 position;

layout(location = 0) out vec4 out_color;

vec3 LinearToSRGB(vec3 linear) {
  // sRGB EOTF (IEC 61966-2-1). The swapchain is UNORM, so encode here.
  vec3 lo = linear * 12.92;
  vec3 hi = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
  return mix(lo, hi, step(vec3(0.0031308), linear));
}

void main() {
  float gaussian_alpha = exp(-0.5f * dot(position, position));
  float alpha = color.a * gaussian_alpha;
  // premultiplied alpha, sRGB-encoded
  out_color = vec4(LinearToSRGB(color.rgb) * alpha, alpha);
}
