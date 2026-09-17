//Copyright (c) Meta Platforms, Inc. and affiliates

#version 460

layout(location = 0) in vec4 rgba;
layout(location = 1) in vec2 position;
layout(location = 2) in flat float ccRecip;

layout(location = 0) out vec4 out_color;

const float ALPHA_THRES = 1.0f / 255.0f;

vec3 LinearToSRGB(vec3 linear) {
  // sRGB EOTF (IEC 61966-2-1). The swapchain is UNORM, so encode here.
  vec3 lo = linear * 12.92;
  vec3 hi = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
  return mix(lo, hi, step(vec3(0.0031308), linear));
}

void main() {
  const float D = 0.5f / (ccRecip + 1.0f / dot(position, position));
  const float alpha = exp(-D) * rgba.a;
  if (alpha < ALPHA_THRES) {
    discard;
  }
  out_color = vec4(LinearToSRGB(rgba.rgb) * alpha, alpha);
}
