# Agent 模块 CMakeLists.txt
# NPC生命线与叙事管理系统 - Agent基础数据结构模块

# ============================================================================
# 源文件列表（相对于当前CMakeLists.txt）
# ============================================================================
set(AGENT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Agent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AgentSerializer.cpp
)

# ============================================================================
# 头文件列表（用于IDE显示）
# ============================================================================
set(AGENT_HEADERS
    ${CMAKE_SOURCE_DIR}/include/naw/agent/Agent.h
    ${CMAKE_SOURCE_DIR}/include/naw/agent/AgentTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/agent/AgentSerialization.h
    ${CMAKE_SOURCE_DIR}/include/naw/agent/AgentSerializer.h
)

# ============================================================================
# 创建静态库
# ============================================================================
add_library(NAW_Agent STATIC
    ${AGENT_SOURCES}
    ${AGENT_HEADERS}
)

# ============================================================================
# 设置包含目录
# ============================================================================
target_include_directories(NAW_Agent
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/render/include
        ${CMAKE_SOURCE_DIR}/third_party/render/third_party/json/single_include
        ${CMAKE_SOURCE_DIR}/third_party/render/third_party/eigen-3.4.0
)

# ============================================================================
# 设置编译选项
# ============================================================================
target_compile_options(NAW_Agent
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
)

# ============================================================================
# 链接依赖库
# ============================================================================
target_link_libraries(NAW_Agent
    PUBLIC
        ${RENDER_ENGINE_LIBRARY}
)

# ============================================================================
# 设置目标属性
# ============================================================================
set_target_properties(NAW_Agent PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# ============================================================================
# 安装规则（可选）
# ============================================================================
install(TARGETS NAW_Agent
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/naw/agent
    DESTINATION include/naw
    FILES_MATCHING PATTERN "*.h"
)

# ============================================================================
# 输出信息
# ============================================================================
message(STATUS "Agent module configured:")
message(STATUS "  Sources: ${AGENT_SOURCES}")
message(STATUS "  Headers: ${AGENT_HEADERS}")

