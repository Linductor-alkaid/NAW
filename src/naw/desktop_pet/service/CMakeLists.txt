# Service Foundation 模块（ErrorHandler 等基础设施）

option(BUILD_SERVICE_FOUNDATION_TESTING "Build service foundation tests" OFF)
option(BUILD_APICLIENT_CHAT_EXAMPLE "Build api_client_chat_example" ON)
option(BUILD_FUNCTION_CALLING_EXAMPLE "Build function_calling_example" ON)
option(BUILD_STT_LLM_VOICE_CHAT_EXAMPLE "Build stt_llm_voice_chat_example" OFF)

set(SERVICE_FOUNDATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ErrorHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ModelManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TaskRouter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ContextManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RequestManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CacheManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ResponseHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ToolManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeTools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/CodeToolsUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/GitIgnoreParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ProjectWhitelist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ProjectStructureCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ReadFileTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/WriteFileTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ListFilesTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/SearchCodeTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/GetProjectStructureTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/AnalyzeCodeTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FunctionCallingHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ToolCallContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ProjectContextCollector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpeechService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScreenCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VisionLayer0.cpp
)

# Windows平台屏幕采集源文件
if(WIN32)
    list(APPEND SERVICE_FOUNDATION_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/ScreenCaptureWindows.cpp
    )
endif()

set(SERVICE_FOUNDATION_HEADERS
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ErrorTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ErrorHandler.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ConfigManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/APIClient.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ModelManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/TaskRouter.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ContextManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/RequestManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/CacheManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ResponseHandler.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ToolManager.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/CodeTools.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/tools/CodeToolsUtils.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/tools/CMakeParser.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/tools/GitIgnoreParser.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/tools/ProjectWhitelist.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/tools/ProjectStructureCache.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/FunctionCallingHandler.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ToolCallContext.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ProjectContextCollector.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/SpeechService.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ScreenCapture.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/ImageProcessor.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/VisionLayer0.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/CommonTypes.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/TaskType.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/TaskPriority.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/ChatMessage.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/ModelConfig.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/RequestResponse.h
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/types/ImageData.h
)

# Windows平台屏幕采集头文件
if(WIN32)
    list(APPEND SERVICE_FOUNDATION_HEADERS
        ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/platform/ScreenCaptureWindows.h
    )
endif()

add_library(NAW_ServiceFoundation STATIC
    ${SERVICE_FOUNDATION_SOURCES}
    ${SERVICE_FOUNDATION_HEADERS}
)

target_include_directories(NAW_ServiceFoundation
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/json/single_include
        ${CMAKE_SOURCE_DIR}/third_party/opencv/include/opencv4
)

target_compile_options(NAW_ServiceFoundation
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(NAW_ServiceFoundation
    PUBLIC
        NAW_ServiceAPIClient
        NAW_ServiceUtils
)

# OpenCV 集成
set(OPENCV_DIR ${CMAKE_SOURCE_DIR}/third_party/opencv)
set(OPENCV_LIB_DIR ${OPENCV_DIR}/lib)
set(OPENCV_BIN_DIR ${OPENCV_DIR}/bin)

# OpenCV 核心库
target_link_libraries(NAW_ServiceFoundation
    PRIVATE
        ${OPENCV_LIB_DIR}/opencv_core4.lib
        ${OPENCV_LIB_DIR}/opencv_imgproc4.lib
        ${OPENCV_LIB_DIR}/opencv_imgcodecs4.lib
)

# Windows平台屏幕采集依赖
if(WIN32)
    # DXGI和D3D11（Windows SDK的一部分）
    # Psapi用于进程枚举
    # Windows Runtime用于Graphics.Capture API
    target_link_libraries(NAW_ServiceFoundation
        PRIVATE
            dxgi
            d3d11
            psapi
            windowsapp  # Windows Runtime (for Graphics.Capture)
            dcomp       # DirectComposition (for Graphics.Capture interop)
    )
    
    # 设置 OpenCV DLL 搜索路径（Windows）
    set_target_properties(NAW_ServiceFoundation PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${OPENCV_BIN_DIR};%PATH%"
    )
endif()

set_target_properties(NAW_ServiceFoundation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

message(STATUS "Service Foundation module configured: NAW_ServiceFoundation")

# APIClient（依赖 ServiceUtils 的 HttpClient，因此单独建库，避免形成循环依赖）
add_library(NAW_ServiceAPIClient STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/APIClient.cpp
    ${CMAKE_SOURCE_DIR}/include/naw/desktop_pet/service/APIClient.h
)

target_link_libraries(NAW_ServiceAPIClient
    PUBLIC
        NAW_ServiceUtils
)

target_include_directories(NAW_ServiceAPIClient
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/json/single_include
)

target_compile_options(NAW_ServiceAPIClient
    PRIVATE
        ${PROJECT_COMPILE_OPTIONS}
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

set_target_properties(NAW_ServiceAPIClient PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

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

    add_executable(APIClientTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/APIClientTest.cpp
    )
    if(MSVC)
        target_compile_options(APIClientTest PRIVATE /GL-)
        target_link_options(APIClientTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(APIClientTest PRIVATE NAW_ServiceAPIClient)
    target_include_directories(APIClientTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME APIClientTest COMMAND APIClientTest)

    add_executable(ModelManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ModelManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(ModelManagerTest PRIVATE /GL-)
        target_link_options(ModelManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ModelManagerTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ModelManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ModelManagerTest COMMAND ModelManagerTest)

    add_executable(TaskRouterTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/TaskRouterTest.cpp
    )
    if(MSVC)
        target_compile_options(TaskRouterTest PRIVATE /GL-)
        target_link_options(TaskRouterTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(TaskRouterTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(TaskRouterTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME TaskRouterTest COMMAND TaskRouterTest)

    add_executable(ContextManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ContextManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(ContextManagerTest PRIVATE /GL-)
        target_link_options(ContextManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ContextManagerTest PRIVATE NAW_ServiceFoundation NAW_ServiceUtils)
    target_include_directories(ContextManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ContextManagerTest COMMAND ContextManagerTest)

    add_executable(RequestManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/RequestManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(RequestManagerTest PRIVATE /GL-)
        target_link_options(RequestManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(RequestManagerTest PRIVATE NAW_ServiceFoundation NAW_ServiceAPIClient NAW_ServiceUtils)
    target_include_directories(RequestManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME RequestManagerTest COMMAND RequestManagerTest)

    add_executable(CacheManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/CacheManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(CacheManagerTest PRIVATE /GL-)
        target_link_options(CacheManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(CacheManagerTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(CacheManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME CacheManagerTest COMMAND CacheManagerTest)

    add_executable(ResponseHandlerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ResponseHandlerTest.cpp
    )
    if(MSVC)
        target_compile_options(ResponseHandlerTest PRIVATE /GL-)
        target_link_options(ResponseHandlerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ResponseHandlerTest PRIVATE NAW_ServiceFoundation NAW_ServiceAPIClient)
    target_include_directories(ResponseHandlerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ResponseHandlerTest COMMAND ResponseHandlerTest)

    add_executable(Phase4IntegrationTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/Phase4IntegrationTest.cpp
    )
    if(MSVC)
        target_compile_options(Phase4IntegrationTest PRIVATE /GL-)
        target_link_options(Phase4IntegrationTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(Phase4IntegrationTest PRIVATE NAW_ServiceFoundation NAW_ServiceAPIClient NAW_ServiceUtils)
    target_include_directories(Phase4IntegrationTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME Phase4IntegrationTest COMMAND Phase4IntegrationTest)

    add_executable(ToolManagerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ToolManagerTest.cpp
    )
    if(MSVC)
        target_compile_options(ToolManagerTest PRIVATE /GL-)
        target_link_options(ToolManagerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ToolManagerTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ToolManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ToolManagerTest COMMAND ToolManagerTest)

    add_executable(CodeToolsTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/CodeToolsTest.cpp
    )
    if(MSVC)
        target_compile_options(CodeToolsTest PRIVATE /GL-)
        target_link_options(CodeToolsTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(CodeToolsTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(CodeToolsTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME CodeToolsTest COMMAND CodeToolsTest)

    add_executable(FunctionCallingHandlerTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/FunctionCallingHandlerTest.cpp
    )
    if(MSVC)
        target_compile_options(FunctionCallingHandlerTest PRIVATE /GL-)
        target_link_options(FunctionCallingHandlerTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(FunctionCallingHandlerTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(FunctionCallingHandlerTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME FunctionCallingHandlerTest COMMAND FunctionCallingHandlerTest)

    add_executable(ToolLLMIntegrationTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ToolLLMIntegrationTest.cpp
    )
    if(MSVC)
        target_compile_options(ToolLLMIntegrationTest PRIVATE /GL-)
        target_link_options(ToolLLMIntegrationTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ToolLLMIntegrationTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ToolLLMIntegrationTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ToolLLMIntegrationTest COMMAND ToolLLMIntegrationTest)

    add_executable(ProjectContextCollectorTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectContextCollectorTest.cpp
    )
    if(MSVC)
        target_compile_options(ProjectContextCollectorTest PRIVATE /GL-)
        target_link_options(ProjectContextCollectorTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ProjectContextCollectorTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ProjectContextCollectorTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ProjectContextCollectorTest COMMAND ProjectContextCollectorTest)

    add_executable(SpeechServiceTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/SpeechServiceTest.cpp
    )
    if(MSVC)
        target_compile_options(SpeechServiceTest PRIVATE /GL-)
        target_link_options(SpeechServiceTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(SpeechServiceTest PRIVATE NAW_ServiceFoundation NAW_ServiceUtils)
    target_include_directories(SpeechServiceTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME SpeechServiceTest COMMAND SpeechServiceTest)

    add_executable(ScreenCaptureTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ScreenCaptureTest.cpp
    )
    if(MSVC)
        target_compile_options(ScreenCaptureTest PRIVATE /GL-)
        target_link_options(ScreenCaptureTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ScreenCaptureTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ScreenCaptureTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    # Windows平台需要链接DXGI和D3D11
    if(WIN32)
        target_link_libraries(ScreenCaptureTest PRIVATE dxgi d3d11)
        # 设置 OpenCV DLL 搜索路径
        set_target_properties(ScreenCaptureTest PROPERTIES
            VS_DEBUGGER_ENVIRONMENT "PATH=${OPENCV_BIN_DIR};%PATH%"
        )
    endif()
    add_test(NAME ScreenCaptureTest COMMAND ScreenCaptureTest)

    add_executable(ImageProcessorTest
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ImageProcessorTest.cpp
    )
    if(MSVC)
        target_compile_options(ImageProcessorTest PRIVATE /GL-)
        target_link_options(ImageProcessorTest PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(ImageProcessorTest PRIVATE NAW_ServiceFoundation)
    target_include_directories(ImageProcessorTest PRIVATE ${CMAKE_SOURCE_DIR}/include)
    # Windows平台需要设置 OpenCV DLL 路径
    if(WIN32)
        set_target_properties(ImageProcessorTest PROPERTIES
            VS_DEBUGGER_ENVIRONMENT "PATH=${OPENCV_BIN_DIR};%PATH%"
        )
    endif()
    add_test(NAME ImageProcessorTest COMMAND ImageProcessorTest)

    add_executable(VisionLayer0Test
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/VisionLayer0Test.cpp
    )
    if(MSVC)
        target_compile_options(VisionLayer0Test PRIVATE /GL-)
        target_link_options(VisionLayer0Test PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(VisionLayer0Test PRIVATE NAW_ServiceFoundation)
    target_include_directories(VisionLayer0Test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    # Windows平台需要设置 OpenCV DLL 路径
    if(WIN32)
        set_target_properties(VisionLayer0Test PROPERTIES
            VS_DEBUGGER_ENVIRONMENT "PATH=${OPENCV_BIN_DIR};%PATH%"
        )
    endif()
    add_test(NAME VisionLayer0Test COMMAND VisionLayer0Test)

endif()

if(BUILD_APICLIENT_CHAT_EXAMPLE)
    add_executable(api_client_chat_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/api_client_chat_example.cpp
    )
    if(MSVC)
        target_compile_options(api_client_chat_example PRIVATE /GL-)
        target_link_options(api_client_chat_example PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(api_client_chat_example PRIVATE NAW_ServiceAPIClient)
    target_include_directories(api_client_chat_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

if(BUILD_FUNCTION_CALLING_EXAMPLE)
    add_executable(function_calling_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/function_calling_example.cpp
    )
    if(MSVC)
        target_compile_options(function_calling_example PRIVATE /GL- /utf-8)
        target_link_options(function_calling_example PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    target_link_libraries(function_calling_example PRIVATE NAW_ServiceFoundation NAW_ServiceAPIClient)
    target_include_directories(function_calling_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

if(BUILD_STT_LLM_VOICE_CHAT_EXAMPLE)
    add_executable(stt_llm_voice_chat_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/stt_llm_voice_chat_example.cpp
    )
    if(MSVC)
        target_compile_options(stt_llm_voice_chat_example PRIVATE /GL-)
        target_link_options(stt_llm_voice_chat_example PRIVATE /LTCG:OFF /INCREMENTAL)
    endif()
    # APIClient 已 PUBLIC 依赖 NAW_ServiceUtils，因此示例能直接使用 HttpClient/AudioProcessor
    target_link_libraries(stt_llm_voice_chat_example PRIVATE NAW_ServiceAPIClient)
    target_include_directories(stt_llm_voice_chat_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

