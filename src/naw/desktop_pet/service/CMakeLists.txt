# Service Foundation 模块（ErrorHandler 等基础设施）

option(BUILD_SERVICE_FOUNDATION_TESTING "Build service foundation tests" OFF)

set(SERVICE_FOUNDATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ErrorHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigManager.cpp
)

set(SERVICE_FOUNDATION_HEADERS
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ErrorTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ErrorHandler.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ConfigManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/CommonTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/TaskType.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/TaskPriority.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/ChatMessage.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/ModelConfig.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/RequestResponse.h
)

add_library(NAW_ServiceFoundation STATIC
    ${SERVICE_FOUNDATION_SOURCES}
    ${SERVICE_FOUNDATION_HEADERS}
)

target_include_directories(NAW_ServiceFoundation
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/json/single_include
)

target_compile_options(NAW_ServiceFoundation
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

set_target_properties(NAW_ServiceFoundation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

message(STATUS "Service Foundation module configured: NAW_ServiceFoundation")

include(CTest)
if(BUILD_TESTING)
    add_executable(ErrorHandlerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ErrorHandlerTest.cpp
    )
    if(MSVC)
        target_compile_options(ErrorHandlerTest PRIVATE /GL-)
        target_link_options(ErrorHandlerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    # ErrorHandler 依赖 HttpResponse::asJson()，其实现位于 NAW_ServiceUtils（HttpSerialization.cpp）
    target_link_libraries(ErrorHandlerTest PRIVATE NAW_ServiceFoundation NAW_ServiceUtils)
    target_include_directories(ErrorHandlerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ErrorHandlerTest COMMAND ErrorHandlerTest)

    add_executable(ConfigManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ConfigManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(ConfigManagerTest PRIVATE /GL-)
        target_link_options(ConfigManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ConfigManagerTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ConfigManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ConfigManagerTest COMMAND ConfigManagerTest)

    add_executable(TypesTaskTypeTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TypesTaskTypeTest.cpp
    )
    if(MSVC)
        target_compile_options(TypesTaskTypeTest PRIVATE /GL-)
        target_link_options(TypesTaskTypeTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(TypesTaskTypeTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(TypesTaskTypeTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME TypesTaskTypeTest COMMAND TypesTaskTypeTest)

    add_executable(TypesChatMessageTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TypesChatMessageTest.cpp
    )
    if(MSVC)
        target_compile_options(TypesChatMessageTest PRIVATE /GL-)
        target_link_options(TypesChatMessageTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(TypesChatMessageTest PRIVATE NAW_ServiceFoundation NAW_ServiceUtils)
    target_include_directories(TypesChatMessageTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME TypesChatMessageTest COMMAND TypesChatMessageTest)

    add_executable(TypesModelConfigTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TypesModelConfigTest.cpp
    )
    if(MSVC)
        target_compile_options(TypesModelConfigTest PRIVATE /GL-)
        target_link_options(TypesModelConfigTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(TypesModelConfigTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(TypesModelConfigTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME TypesModelConfigTest COMMAND TypesModelConfigTest)

    add_executable(TypesRequestResponseTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TypesRequestResponseTest.cpp
    )
    if(MSVC)
        target_compile_options(TypesRequestResponseTest PRIVATE /GL-)
        target_link_options(TypesRequestResponseTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(TypesRequestResponseTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(TypesRequestResponseTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME TypesRequestResponseTest COMMAND TypesRequestResponseTest)
endif()

