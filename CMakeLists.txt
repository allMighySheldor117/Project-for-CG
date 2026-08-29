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
    src/AuthoredChamber.cpp
    src/Camera.cpp
    src/CaveGeneration.cpp
    src/CaveSceneFingerprint.cpp
    src/CaveSceneGeneration.cpp
    src/ChamberTemplates.cpp
    src/ChamberStructure.cpp
    src/CaveSceneValidation.cpp
    src/CommandLine.cpp
    src/CrystalCollection.cpp
    src/DeterministicRandom.cpp
    src/ElementalVisuals.cpp
    src/ExitArch.cpp
    src/FrameProfile.cpp
    src/GameLoop.cpp
    src/Generation.cpp
    src/GenerationTypes.cpp
    src/GeometryFingerprint.cpp
    src/GeometryMesh.cpp
    src/GeometryValidation.cpp
    src/MeshData.cpp
    src/MazeGeneration.cpp
    src/ObjLoader.cpp
    src/PlayerController.cpp
    src/ProfileTraversal.cpp
    src/Reachability.cpp
    src/Rendering.cpp
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
    src/GpuTexture.cpp
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

    add_library(crystalbound_test_support STATIC
        tests/support/SeedCorpus.cpp
        tests/support/SeedManifest.cpp
    )
    target_compile_features(crystalbound_test_support PUBLIC cxx_std_17)
    set_target_properties(crystalbound_test_support PROPERTIES
        CXX_EXTENSIONS OFF
    )
    target_include_directories(crystalbound_test_support
        PUBLIC
            tests/support
    )
    target_link_libraries(crystalbound_test_support
        PUBLIC
            crystalbound_core
    )
    crystalbound_enable_first_party_warnings(crystalbound_test_support)

    add_executable(crystalbound_tests
        tests/unit/CaveSceneTests.cpp
        tests/unit/CrystalCollectionTests.cpp
        tests/unit/ElementalVisualsTests.cpp
        tests/unit/FrameProfileTests.cpp
        tests/unit/GeometryTests.cpp
        tests/unit/GenerationTests.cpp
        tests/unit/GameLoopTests.cpp
        tests/unit/PlayerControllerTests.cpp
        tests/unit/ReachabilityTests.cpp
        tests/unit/RenderingTests.cpp
        tests/unit/SeedCorpusTests.cpp
        tests/unit/TestMain.cpp
    )
    target_compile_features(crystalbound_tests PRIVATE cxx_std_17)
    set_target_properties(crystalbound_tests PROPERTIES
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(crystalbound_tests
        PRIVATE
            crystalbound_core
            crystalbound_test_support
    )
    target_include_directories(crystalbound_tests PRIVATE tests/support)
    crystalbound_enable_first_party_warnings(crystalbound_tests)

    add_custom_command(
        TARGET crystalbound_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/WaterChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/WaterChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/WaterChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/WaterChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/EarthChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/EarthChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/EarthChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/EarthChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/AirChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/AirChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/AirChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/AirChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/FireChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/FireChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/FireChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/FireChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/AetherChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/AetherChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/AetherChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/AetherChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ExitChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/ExitChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ExitChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/ExitChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/StartChamber.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/StartChamber.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/StartChamber.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/StartChamber.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ElementalCrystal.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/ElementalCrystal.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ElementalCrystal.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/ElementalCrystal.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeRoom.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeRoom.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeRoom.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeRoom.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeWall.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeWall.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeWall.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeWall.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazePillar.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazePillar.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazePillar.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazePillar.mtl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeArch.obj"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeArch.obj"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/MazeArch.mtl"
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata/MazeArch.mtl"
       COMMENT "Copying Crystalbound test fixtures"
        VERBATIM
    )

    add_test(
        NAME crystalbound_core_tests
        COMMAND crystalbound_tests
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata"
    )
    add_test(
        NAME crystalbound_structural_tests
        COMMAND crystalbound_tests
                "$<TARGET_FILE_DIR:crystalbound_tests>/testdata"
                --exclude-golden
    )
    set_tests_properties(crystalbound_structural_tests PROPERTIES
        LABELS structural
    )

    add_executable(crystalbound_seed_corpus
        tests/tools/SeedCorpusMain.cpp
    )
    target_compile_features(crystalbound_seed_corpus PRIVATE cxx_std_17)
    set_target_properties(crystalbound_seed_corpus PROPERTIES
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(crystalbound_seed_corpus
        PRIVATE
            crystalbound_test_support
    )
    crystalbound_enable_first_party_warnings(crystalbound_seed_corpus)

    add_executable(crystalbound_profile_runner
        src/Application.cpp
        src/GpuMesh.cpp
        src/GpuTexture.cpp
        src/ResourcePaths.cpp
        src/ShaderProgram.cpp
        tests/tools/ProfileTraversalMain.cpp
    )
    target_compile_features(crystalbound_profile_runner PRIVATE cxx_std_17)
    set_target_properties(crystalbound_profile_runner PROPERTIES
        CXX_EXTENSIONS OFF
    )
    target_include_directories(crystalbound_profile_runner PRIVATE include)
    target_compile_definitions(crystalbound_profile_runner PRIVATE GLFW_INCLUDE_NONE)
    if(WIN32)
        target_compile_definitions(crystalbound_profile_runner PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
    target_link_libraries(crystalbound_profile_runner
        PRIVATE
            glad
            glfw
            glm::glm
            imgui
            crystalbound_core
    )
    crystalbound_enable_first_party_warnings(crystalbound_profile_runner)
    foreach(resource_directory IN ITEMS assets shaders)
        add_custom_command(
            TARGET crystalbound_profile_runner
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:crystalbound_profile_runner>/resources/${resource_directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/${resource_directory}"
                    "$<TARGET_FILE_DIR:crystalbound_profile_runner>/resources/${resource_directory}"
            COMMENT "Copying ${resource_directory} resources for traversal profile"
            VERBATIM
        )
    endforeach()
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
