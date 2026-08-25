#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_mvp;

out vec3 normal_color;

void main()
{
    normal_color = normalize(a_normal) * 0.5 + 0.5;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
