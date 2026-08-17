#ifndef GUI_SHADERS_RECT_H
#define GUI_SHADERS_RECT_H

namespace Gui::Shaders {

constexpr const char *rectVertex = R"foo(
#version 120
attribute vec4 vertex;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
}
)foo";

constexpr const char *rectFragment = R"foo(
#version 120

uniform vec4 fillColor;

void main()
{
    gl_FragColor = fillColor;
}
)foo";

}

#endif // GUI_SHADERS_RECT_H
