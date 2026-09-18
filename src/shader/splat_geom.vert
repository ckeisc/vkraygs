#version 460

// Buffer layout from projection.comp: 3x vec4 per instance (48-byte stride)
//   offset 0:  vec4(ndc_position.xyz, padding)
//   offset 16: vec4(rot_scale)  [2x vec2 packed]
//   offset 32: vec4(color.rgb, opacity)
layout(location = 0) in vec4 ndc_position;
layout(location = 1) in vec4 scale_rot;
layout(location = 2) in vec4 color_opacity;

layout(location = 0) out vec4 out_scale_rot;
layout(location = 1) out vec4 out_color_opacity;

void main() {
  gl_Position = vec4(ndc_position.xyz, 1.f);
  out_scale_rot = scale_rot;
  out_color_opacity = color_opacity;
}
