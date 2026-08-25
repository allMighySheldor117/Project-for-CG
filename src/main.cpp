#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <tiny_obj_loader.h>

#include <iostream>

int main()
{
    int glfw_major{};
    int glfw_minor{};
    int glfw_revision{};
    glfwGetVersion(&glfw_major, &glfw_minor, &glfw_revision);

    const glm::vec3 origin{0.0F, 0.0F, 0.0F};
    const ImVec2 ui_origin{0.0F, 0.0F};
    const tinyobj::attrib_t empty_attributes{};
    const auto glad_loader_entry = &gladLoaderLoadGL;

    std::cout << "NPR renderer offline foundation is ready.\n"
              << "GLFW runtime: " << glfw_major << '.' << glfw_minor << '.'
              << glfw_revision << '\n'
              << "GLM origin: " << origin.x << ", " << origin.y << ", "
              << origin.z << '\n'
              << "Dear ImGui: " << IMGUI_VERSION << " at (" << ui_origin.x
              << ", " << ui_origin.y << ")\n"
              << "tinyobjloader empty vertex count: "
              << empty_attributes.vertices.size() << '\n'
              << "GLAD loader symbol linked: " << std::boolalpha
              << (glad_loader_entry != nullptr) << '\n';

    return 0;
}
