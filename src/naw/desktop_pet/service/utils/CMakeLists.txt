# Service Utils 模块 CMakeLists.txt
# HTTP客户端工具模块

# ============================================================================
# 源文件列表
# ============================================================================
set(SERVICE_UTILS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpClient.cpp
)

# ============================================================================
# 头文件列表（用于IDE显示）
# ============================================================================
set(SERVICE_UTILS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/HttpClient.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/utils/HttpTypes.h
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
        ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
)

# ============================================================================
# 设置编译选项
# ============================================================================
target_compile_options(NAW_ServiceUtils
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
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
