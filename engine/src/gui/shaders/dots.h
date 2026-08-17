#ifndef GUI_SHADERS_DOTS_H
#define GUI_SHADERS_DOTS_H

namespace Gui::Shaders {

// Batched dot rendering: one interleaved buffer carries every dot of a track and
// two draw calls (outline pass, fill pass) replace the per-dot bind/upload/draw
// cycle that dominated frame time once the tracks ran at 50 updates a second.
constexpr const char *dotsVertex = R"foo(
#version 120
attribute vec2 corner;      // quad corner in [-1.5, 1.5] units of the dot radius
attribute vec2 center;      // dot center, painter pixels
attribute float radiusBase; // unscaled dot radius
attribute vec3 dotColor;    // per-dot fill color

uniform mat4 projection;
uniform float radiusAdd;    // additive outline growth, applied before scaling
uniform float radiusScale;  // zoom/DPI scale factor

varying vec2 vCenter;
varying float vRadius;
varying vec3 vColor;

void main()
{
    float r = (radiusBase + radiusAdd) * radiusScale;
    vec2 pos = center + corner * r;
    vCenter = center;
    vRadius = r;
    vColor = dotColor;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
}
)foo";

constexpr const char *dotsFragment = R"foo(
#version 120

varying vec2 vCenter;
varying float vRadius;
varying vec3 vColor;

void main()
{
    float d = length(vCenter - gl_FragCoord.xy) - vRadius;
    float t = clamp(d, 0.0, 1.0);
    gl_FragColor = vec4(vColor, 1.0 - t);
}
)foo";

}

#endif // GUI_SHADERS_DOTS_H
