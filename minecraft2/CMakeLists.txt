cmake_minimum_required(VERSION 3.24)
project(MinecraftClone VERSION 1.0.0 LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use MSVC multithreading and AVX2 if on Windows
if(MSVC)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:/MP>
        $<$<COMPILE_LANGUAGE:C,CXX>:/W4>
        $<$<COMPILE_LANGUAGE:C,CXX>:/WX->
        $<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>
    )
else()
    add_compile_options(-Wall -Wextra -pthread)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# GLFW
message(STATUS "Fetching GLFW...")
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM
message(STATUS "Fetching GLM...")
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# glslang (for runtime shader compilation)
message(STATUS "Fetching glslang...")
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        main
)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glslang)

# Vulkan-Headers
message(STATUS "Fetching Vulkan-Headers...")
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(vulkan_headers)

# Volk (Vulkan meta loader)
message(STATUS "Fetching Volk...")
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(volk)

# Ensure volk can find Vulkan headers
target_link_libraries(volk PUBLIC Vulkan::Headers)

# ------------------------------------------------------------------------------
# Application Target
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.c" "src/*.asm")
file(GLOB_RECURSE HEADER_FILES "src/*.h" "src/*.hpp")

add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glm::glm
    volk::volk
    Vulkan::Headers
    glslang::glslang
    glslang::SPIRV
    glslang-default-resource-limits
)

if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()
