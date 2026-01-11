cmake_minimum_required(VERSION 3.15)
project(NAW_System VERSION 1.0.0 LANGUAGES C CXX)

# 全局启用 CTest，生成测试相关目标与配置
include(CTest)
set(BUILD_TESTING ON CACHE BOOL "Enable building tests" FORCE)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    set(PROJECT_COMPILE_OPTIONS /utf-8 /W4 /WX- /FS)
else()
    set(PROJECT_COMPILE_OPTIONS -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# 第三方库路径
# ============================================================================
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# ============================================================================
# Render 引擎配置
# ============================================================================
set(RENDER_DIR ${THIRD_PARTY_DIR}/render)
set(RENDER_PREBUILT_DIR ${RENDER_DIR}/RenderEngine-prebuilt-Release-x64)

# 优先使用预编译库
if(EXISTS ${RENDER_PREBUILT_DIR}/lib/cmake/RenderEngine/RenderEngineConfig.cmake)
    # 设置 RenderEngine 预编译库路径
    set(RenderEngine_DIR ${RENDER_PREBUILT_DIR}/lib/cmake/RenderEngine)

    message(STATUS "Using RenderEngine prebuilt library: ${RENDER_PREBUILT_DIR}")
    message(STATUS "  CMake config: ${RenderEngine_DIR}")

    # 查找 OpenMP（RenderEngine 预编译库依赖）
    find_package(OpenMP REQUIRED)

    # 查找 RenderEngine 预编译库
    find_package(RenderEngine REQUIRED)

    # 设置预编译库的 shader 资源路径
    set(RENDER_ENGINE_SHADERS_DIR ${RENDER_PREBUILT_DIR}/share/RenderEngine/shaders)

    # 添加 shader 文件到构建（复制到输出目录）
    if(EXISTS ${RENDER_ENGINE_SHADERS_DIR})
        message(STATUS "  Shaders directory: ${RENDER_ENGINE_SHADERS_DIR}")

        # 自定义目标：复制 shaders 到输出目录
        add_custom_command(
            OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${RENDER_ENGINE_SHADERS_DIR}
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
            COMMENT "Copying RenderEngine shaders to output directory"
            VERBATIM
        )

        add_custom_target(RenderEngineShaders ALL
            DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
        )

        message(STATUS "  Shaders will be copied to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    else()
        message(WARNING "RenderEngine shaders directory not found at ${RENDER_ENGINE_SHADERS_DIR}")
    endif()

    set(RENDER_ENGINE_LIBRARY RenderEngine::RenderEngine)
    message(STATUS "RenderEngine prebuilt library configured successfully")

elseif(EXISTS ${RENDER_DIR}/CMakeLists.txt)
    # 回退方案：从源码编译 RenderEngine
    message(WARNING "RenderEngine prebuilt library not found, building from source (slower)")
    message(STATUS "  To use prebuilt library, ensure: ${RENDER_PREBUILT_DIR}")

    # 设置第三方库路径（仅在此作用域有效，不强制覆盖render内部的设置）
    # 这样render库更新时不会受到影响
    set(THIRD_PARTY_DIR ${RENDER_DIR}/third_party)

    # 关闭render的测试和示例编译（使用普通set，不强制缓存）
    set(BUILD_TESTS OFF)
    set(BUILD_EXAMPLES OFF)

    message(STATUS "Adding Render engine as subdirectory: ${RENDER_DIR}")
    message(STATUS "  Third party dir: ${THIRD_PARTY_DIR}")
    message(STATUS "  Build tests: OFF")
    message(STATUS "  Build examples: OFF")

    add_subdirectory(${RENDER_DIR} ${CMAKE_BINARY_DIR}/render EXCLUDE_FROM_ALL)
    set(RENDER_ENGINE_LIBRARY RenderEngine)
    message(STATUS "Render engine configured from source")
else()
    message(FATAL_ERROR "Render engine not found at ${RENDER_DIR}")
endif()

# ============================================================================
# Agent 模块
# ============================================================================
add_subdirectory(src/naw/agent)

# ============================================================================
# Service Utils 模块（HTTP客户端等工具）
# ============================================================================
add_subdirectory(src/naw/desktop_pet/service)
add_subdirectory(src/naw/desktop_pet/service/utils)

# ============================================================================
# 输出配置信息
# ============================================================================
message(STATUS "========================================")
message(STATUS "NAW System Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Render Engine: ${RENDER_ENGINE_LIBRARY}")
message(STATUS "========================================")

