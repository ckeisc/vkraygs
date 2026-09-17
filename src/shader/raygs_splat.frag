//Copyright (c) Meta Platforms, Inc. and affiliates

#version 460

layout(location = 0) in vec4 rgba;
layout(location = 1) in vec2 position;
layout(location = 2) in flat float ccRecip;

layout(location = 0) out vec4 out_color;

const float ALPHA_THRES = 1.0f / 255.0f;

void main() {
  const float D = 0.5f / (ccRecip + 1.0f / dot(position, position));
  const float alpha = exp(-D) * rgba.a;
  if (alpha < ALPHA_THRES) {
    discard;
  }
  out_color = vec4(rgba.rgb * alpha, alpha);
}
