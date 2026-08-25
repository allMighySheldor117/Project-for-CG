#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;

uniform mat4 u_mvp;

out vec3 vertex_color;

void main()
{
    vertex_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
