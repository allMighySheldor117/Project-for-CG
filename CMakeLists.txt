cmake_minimum_required(VERSION 3.24)

project(
    npr_renderer
    VERSION 0.1.0
    DESCRIPTION "Real-time non-photorealistic renderer"
    LANGUAGES C CXX
)

option(NPR_BUILD_TESTS "Build NPR renderer tests" OFF)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)

if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glm EXCLUDE_FROM_ALL)

add_library(glad STATIC
    third_party/glad/src/gl.c
)
target_include_directories(glad
    PUBLIC
        third_party/glad/include
)
target_link_libraries(glad
    PRIVATE
        ${CMAKE_DL_LIBS}
)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui
    PUBLIC
        third_party/imgui
        third_party/imgui/backends
)
target_compile_definitions(imgui
    PRIVATE
        GLFW_INCLUDE_NONE
)
target_link_libraries(imgui
    PUBLIC
        glfw
)

add_library(tinyobjloader INTERFACE)
add_library(tinyobjloader::tinyobjloader ALIAS tinyobjloader)
target_include_directories(tinyobjloader
    INTERFACE
        third_party/tinyobjloader
)

add_executable(npr_renderer
    src/Application.cpp
    src/Camera.cpp
    src/ResourcePaths.cpp
    src/ShaderProgram.cpp
    src/main.cpp
)
target_compile_features(npr_renderer PRIVATE cxx_std_17)
set_target_properties(npr_renderer PROPERTIES
    CXX_EXTENSIONS OFF
)
target_include_directories(npr_renderer
    PRIVATE
        include
)
target_compile_definitions(npr_renderer
    PRIVATE
        GLFW_INCLUDE_NONE
)
if(WIN32)
    target_compile_definitions(npr_renderer PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()
target_link_libraries(npr_renderer
    PRIVATE
        glad
        glfw
        glm::glm
        imgui
        tinyobjloader
)

if(MSVC)
    target_compile_options(npr_renderer PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
    )
else()
    target_compile_options(npr_renderer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(NPR_BUILD_TESTS)
    enable_testing()
    message(STATUS "NPR_BUILD_TESTS is enabled; tests begin in Blueprint Step 4.")
endif()

foreach(resource_directory IN ITEMS assets shaders)
    add_custom_command(
        TARGET npr_renderer
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:npr_renderer>/resources/${resource_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${resource_directory}"
                "$<TARGET_FILE_DIR:npr_renderer>/resources/${resource_directory}"
        COMMENT "Copying ${resource_directory} resources"
        VERBATIM
    )
endforeach()

install(
    TARGETS npr_renderer
    RUNTIME DESTINATION .
)
install(
    DIRECTORY assets shaders
    DESTINATION resources
)
