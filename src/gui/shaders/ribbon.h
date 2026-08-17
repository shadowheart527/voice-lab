#ifndef GUI_SHADERS_RIBBON_H
#define GUI_SHADERS_RIBBON_H

namespace Gui::Shaders {

// Batched track rendering with per-vertex color: used for the connected-line track
// style and for the gender-gradient coloring, where every point along a track
// carries its own masc-to-fem color.
constexpr const char *ribbonVertex = R"foo(
#version 120
attribute vec2 pos;
attribute vec3 vcolor;

uniform mat4 projection;

varying vec3 fcolor;

void main()
{
    fcolor = vcolor;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
}
)foo";

constexpr const char *ribbonFragment = R"foo(
#version 120

uniform float alphaMul;

varying vec3 fcolor;

void main()
{
    gl_FragColor = vec4(fcolor, alphaMul);
}
)foo";

}

#endif // GUI_SHADERS_RIBBON_H
