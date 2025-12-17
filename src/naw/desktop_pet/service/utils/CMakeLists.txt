# 选项控制示例构建
option(BUILD_HTTPCLIENT_EXAMPLE "Build http_client_example" OFF)
option(BUILD_AUDIOPROCESSOR_EXAMPLE "Build audio_processor_example" ON)
option(BUILD_TESTING "Build testing" OFF)
# Service Utils 模块 CMakeLists.txt
# HTTP客户端工具模块

# ============================================================================
# 依赖：cpp-httplib
#  - 优先使用 third_party/cpp-httplib/httplib.h
#  - 若不存在，则通过 FetchContent 拉取
# ============================================================================
set(LOCAL_HTTPLIB_DIR ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib)

if(EXISTS ${LOCAL_HTTPLIB_DIR}/httplib.h)
    message(STATUS "Using bundled cpp-httplib from ${LOCAL_HTTPLIB_DIR}")
    set(HTTPLIB_INCLUDE_DIR ${LOCAL_HTTPLIB_DIR})
else()
    message(STATUS "cpp-httplib not found locally, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        cpp_httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.15.3
    )
    FetchContent_MakeAvailable(cpp_httplib)
    set(HTTPLIB_INCLUDE_DIR ${cpp_httplib_SOURCE_DIR})
endif()

# ============================================================================
# 源文件列表
# ============================================================================
set(SERVICE_UTILS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpSerialization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TokenCounter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TokenUsageClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioProcessor.cpp
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)

# ============================================================================
# 头文件列表（用于IDE显示）
# ============================================================================
set(SERVICE_UTILS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/HttpClient.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/HttpTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/HttpSerialization.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/TokenCounter.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/TokenUsageClient.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/AudioProcessor.h
)

# ============================================================================
# 创建静态库
# ============================================================================
add_library(NAW_ServiceUtils STATIC
    ${SERVICE_UTILS_SOURCES}
    ${SERVICE_UTILS_HEADERS}
)

# ============================================================================
# 设置包含目录
# ============================================================================
target_include_directories(NAW_ServiceUtils
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${HTTPLIB_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/json/single_include
        ${CMAKE_SOURCE_DIR}/third_party/miniaudio
)

# ============================================================================
# 设置编译选项
# ============================================================================
target_compile_options(NAW_ServiceUtils
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(NAW_ServiceUtils
    PRIVATE
        MA_ENABLE_WASAPI
)

# ============================================================================
# 设置目标属性
# ============================================================================
set_target_properties(NAW_ServiceUtils PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "Service Utils module configured:")
message(STATUS "  Sources: ${SERVICE_UTILS_SOURCES}")
message(STATUS "  Headers: ${SERVICE_UTILS_HEADERS}")

# ============================================================================
# 测试与示例（可选）
# ============================================================================

include(CTest)
if(BUILD_TESTING)
    add_executable(HttpClientTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/HttpClientTest.cpp
    )
    add_executable(TokenCounterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TokenCounterTest.cpp
    )
    add_executable(TokenUsageClientTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TokenUsageClientTest.cpp
    )
    if(MSVC)
        # 关闭 HttpClientTest 的 LTCG，避免测试注册符号被链接器裁剪
        target_compile_options(HttpClientTest PRIVATE /GL-)
        target_link_options(HttpClientTest PRIVATE /LTCG:OFF /INCREMENTAL)
        target_compile_options(TokenCounterTest PRIVATE /GL-)
        target_link_options(TokenCounterTest PRIVATE /LTCG:OFF /INCREMENTAL)
        target_compile_options(TokenUsageClientTest PRIVATE /GL-)
        target_link_options(TokenUsageClientTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(HttpClientTest
        PRIVATE
            NAW_ServiceUtils
    )
    target_link_libraries(TokenCounterTest
        PRIVATE
            NAW_ServiceUtils
    )
    target_link_libraries(TokenUsageClientTest
        PRIVATE
            NAW_ServiceUtils
    )
    target_include_directories(HttpClientTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(TokenCounterTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(TokenUsageClientTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME HttpClientTest COMMAND HttpClientTest)
    add_test(NAME TokenCounterTest COMMAND TokenCounterTest)
    add_test(NAME TokenUsageClientTest COMMAND TokenUsageClientTest)
endif()

if(BUILD_HTTPCLIENT_EXAMPLE)
    add_executable(http_client_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/http_client_example.cpp
    )
    target_link_libraries(http_client_example PRIVATE NAW_ServiceUtils)
    target_include_directories(http_client_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

if(BUILD_AUDIOPROCESSOR_EXAMPLE)
    add_executable(audio_processor_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/audio_processor_example.cpp
    )
    target_link_libraries(audio_processor_example PRIVATE NAW_ServiceUtils)
    target_include_directories(audio_processor_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()
