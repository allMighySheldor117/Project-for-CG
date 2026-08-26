cmake_minimum_required(VERSION 3.24)

project(
    crystalbound
    VERSION 0.1.0
    DESCRIPTION "First-person procedural cave exploration game"
    LANGUAGES C CXX
)

option(CRYSTALBOUND_BUILD_TESTS "Build Crystalbound tests" OFF)

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

add_library(tinyobjloader_impl STATIC
    src/TinyObjLoader.cpp
)
target_compile_features(tinyobjloader_impl PRIVATE cxx_std_17)
target_link_libraries(tinyobjloader_impl
    PRIVATE
        tinyobjloader::tinyobjloader
)

add_library(crystalbound_core STATIC
    src/Camera.cpp
    src/CaveGeneration.cpp
    src/CaveSceneFingerprint.cpp
    src/CaveSceneGeneration.cpp
    src/CaveSceneValidation.cpp
    src/CommandLine.cpp
    src/DeterministicRandom.cpp
    src/Generation.cpp
    src/GenerationTypes.cpp
    src/GeometryFingerprint.cpp
    src/GeometryMesh.cpp
    src/GeometryValidation.cpp
    src/MeshData.cpp
    src/ObjLoader.cpp
    src/PlayerController.cpp
    src/TopologyFingerprint.cpp
    src/TopologyGeneration.cpp
    src/TopologyValidation.cpp
    src/SplineGeometry.cpp
)
target_compile_features(crystalbound_core PUBLIC cxx_std_17)
set_target_properties(crystalbound_core PROPERTIES
    CXX_EXTENSIONS OFF
)
target_include_directories(crystalbound_core
    PUBLIC
        include
)
target_link_libraries(crystalbound_core
    PUBLIC
        glm::glm
    PRIVATE
        tinyobjloader::tinyobjloader
        tinyobjloader_impl
)

add_executable(crystalbound
    src/Application.cpp
    src/GpuMesh.cpp
    src/ResourcePaths.cpp
    src/ShaderProgram.cpp
    src/main.cpp
)
target_compile_features(crystalbound PRIVATE cxx_std_17)
set_target_properties(crystalbound PROPERTIES
    CXX_EXTENSIONS OFF
)
target_include_directories(crystalbound
    PRIVATE
        include
)
target_compile_definitions(crystalbound
    PRIVATE
        GLFW_INCLUDE_NONE
)
if(WIN32)
    target_compile_definitions(crystalbound PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()
target_link_libraries(crystalbound
    PRIVATE
        glad
        glfw
        glm::glm
        imgui
        crystalbound_core
)

function(crystalbound_enable_first_party_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

crystalbound_enable_first_party_warnings(crystalbound_core)
crystalbound_enable_first_party_warnings(crystalbound)

if(CRYSTALBOUND_BUILD_TESTS)
    enable_testing()

    add_executable(crystalbound_tests
        tests/unit/CaveSceneTests.cpp
        tests/unit/GeometryTests.cpp
        tests/unit/GenerationTests.cpp
        tests/unit/PlayerControllerTests.cpp
        tests/unit/TestMain.cpp
    )
    target_compile_features(crystalbound_tests PRIVATE cxx_std_17)
    set_target_properties(crystalbound_tests PROPERTIES
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(crystalbound_tests
        PRIVATE
            crystalbound_core
    )
    crystalbound_enable_first_party_warnings(crystalbound_tests)

    add_custom_command(
        TARGET crystalbound_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata"
        COMMENT "Copying Crystalbound test fixtures"
        VERBATIM
    )

    add_test(
        NAME crystalbound_core_tests
        COMMAND crystalbound_tests
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata"
    )
endif()

foreach(resource_directory IN ITEMS assets shaders)
    add_custom_command(
        TARGET crystalbound
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:crystalbound>/resources/${resource_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${resource_directory}"
                "$<TARGET_FILE_DIR:crystalbound>/resources/${resource_directory}"
        COMMENT "Copying ${resource_directory} resources"
        VERBATIM
    )
endforeach()

install(
    TARGETS crystalbound
    RUNTIME DESTINATION .
)
install(
    DIRECTORY assets shaders
    DESTINATION resources
)
