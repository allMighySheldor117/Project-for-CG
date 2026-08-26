#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texture_coordinates;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat3 u_normal_matrix;

out vec3 world_position;
out vec3 world_normal;
out vec2 texture_coordinates;

void main()
{
    vec4 position = u_model * vec4(a_position, 1.0);
    world_position = position.xyz;
    world_normal = normalize(u_normal_matrix * a_normal);
    texture_coordinates = a_texture_coordinates;
    gl_Position = u_projection * u_view * position;
}
