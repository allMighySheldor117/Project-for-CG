#version 330 core

const int maximum_point_lights = 8;
const int rock_material = 0;
const float minimum_weight_sum = 0.000001;

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float attenuation_constant;
    float attenuation_linear;
    float attenuation_quadratic;
    float range_metres;
};

in vec3 world_position;
in vec3 world_normal;
in vec2 texture_coordinates;

uniform vec3 u_camera_position;
uniform vec3 u_albedo;
uniform vec3 u_material_ambient;
uniform vec3 u_material_diffuse;
uniform vec3 u_material_specular;
uniform vec3 u_material_emission;
uniform float u_material_shininess;
uniform float u_texture_scale;
uniform float u_triplanar_sharpness;
uniform int u_material_kind;
uniform sampler2D u_rock_texture;
uniform sampler2D u_wood_texture;
uniform int u_point_light_count;
uniform PointLight u_point_lights[maximum_point_lights];
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;

out vec4 fragment_color;

float rock_noise(vec3 normal)
{
    vec3 weights = pow(max(abs(normal), vec3(minimum_weight_sum)),
        vec3(u_triplanar_sharpness));
    weights /= max(weights.x + weights.y + weights.z, minimum_weight_sum);
    float x_projection = texture(u_rock_texture, world_position.yz * u_texture_scale).r;
    float y_projection = texture(u_rock_texture, world_position.xz * u_texture_scale).r;
    float z_projection = texture(u_rock_texture, world_position.xy * u_texture_scale).r;
    return dot(vec3(x_projection, y_projection, z_projection), weights);
}

vec3 surface_color(vec3 normal)
{
    if (u_material_kind == rock_material) {
        return u_albedo * mix(0.54, 1.16, rock_noise(normal));
    }
    vec2 grain_coordinates = texture_coordinates * vec2(u_texture_scale, 1.0);
    return u_albedo * texture(u_wood_texture, grain_coordinates).rgb;
}

void main()
{
    vec3 normal = normalize(world_normal);
    vec3 view_direction = normalize(u_camera_position - world_position);
    vec3 albedo = surface_color(normal);
    vec3 linear_color = u_material_ambient * albedo + u_material_emission;

    for (int index = 0; index < u_point_light_count; ++index) {
        PointLight light = u_point_lights[index];
        vec3 to_light = light.position - world_position;
        float distance_metres = length(to_light);
        vec3 light_direction = distance_metres > minimum_weight_sum
            ? to_light / distance_metres : normal;
        float diffuse_amount = max(dot(normal, light_direction), 0.0);
        vec3 reflected_direction = reflect(-light_direction, normal);
        float specular_amount = diffuse_amount > 0.0
            ? pow(max(dot(view_direction, reflected_direction), 0.0), u_material_shininess)
            : 0.0;
        float attenuation_denominator = light.attenuation_constant
            + light.attenuation_linear * distance_metres
            + light.attenuation_quadratic * distance_metres * distance_metres;
        float local_cutoff = 1.0 - smoothstep(
            light.range_metres * 0.72, light.range_metres, distance_metres);
        float attenuation = local_cutoff / max(attenuation_denominator, minimum_weight_sum);
        vec3 diffuse = u_material_diffuse * albedo * diffuse_amount;
        vec3 specular = u_material_specular * specular_amount;
        linear_color += (diffuse + specular) * light.color * light.intensity * attenuation;
    }

    float camera_distance = length(u_camera_position - world_position);
    float fog = clamp((camera_distance - u_fog_start) / max(u_fog_end - u_fog_start,
        minimum_weight_sum), 0.0, 1.0);
    linear_color = mix(linear_color, u_fog_color, fog);
    fragment_color = vec4(max(linear_color, vec3(0.0)), 1.0);
}
