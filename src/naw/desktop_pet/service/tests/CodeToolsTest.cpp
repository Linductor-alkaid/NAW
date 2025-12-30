#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/ToolManager.h"
#include "naw/desktop_pet/service/ErrorHandler.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace naw::desktop_pet::service;
namespace fs = std::filesystem;

// 轻量自测断言工具（复用现有测试风格）
namespace mini_test {

inline std::string toString(const std::string& v) { return v; }
inline std::string toString(const char* v) { return v ? std::string(v) : "null"; }
inline std::string toString(bool v) { return v ? "true" : "false"; }

template <typename T>
std::string toString(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

class AssertionFailed : public std::runtime_error {
public:
    explicit AssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define CHECK_TRUE(cond)                                                                          \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw mini_test::AssertionFailed(std::string("CHECK_TRUE failed: ") + #cond);         \
    } while (0)

#define CHECK_FALSE(cond) CHECK_TRUE(!(cond))

#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        const auto _va = (a);                                                                     \
        const auto _vb = (b);                                                                     \
        if (!(_va == _vb)) {                                                                      \
            throw mini_test::AssertionFailed(std::string("CHECK_EQ failed: ") + #a " vs " #b +    \
                                             " (" + mini_test::toString(_va) + " vs " +           \
                                             mini_test::toString(_vb) + ")");                     \
        }                                                                                         \
    } while (0)

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline int run(const std::vector<TestCase>& tests) {
    int failed = 0;
    for (const auto& t : tests) {
        try {
            t.fn();
            std::cout << "[  OK  ] " << t.name << "\n";
        } catch (const AssertionFailed& e) {
            failed++;
            std::cout << "[ FAIL ] " << t.name << " :: " << e.what() << "\n";
        } catch (const std::exception& e) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: " << e.what() << "\n";
        } catch (...) {
            failed++;
            std::cout << "[ EXC  ] " << t.name << " :: unknown exception\n";
        }
    }
    std::cout << "Executed " << tests.size() << " cases, failed " << failed << ".\n";
    return failed == 0 ? 0 : 1;
}

} // namespace mini_test

// ========== 测试辅助函数 ==========

namespace {
    // 创建临时测试目录
    fs::path createTempTestDir() {
        fs::path tempDir = fs::temp_directory_path() / "CodeToolsTest";
        if (fs::exists(tempDir)) {
            fs::remove_all(tempDir);
        }
        fs::create_directories(tempDir);
        return tempDir;
    }
    
    // 清理临时测试目录
    void cleanupTempTestDir(const fs::path& dir) {
        try {
            if (fs::exists(dir)) {
                fs::remove_all(dir);
            }
        } catch (...) {
            // 忽略清理错误
        }
    }
    
    // 创建测试文件
    void createTestFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path);
        CHECK_TRUE(file.is_open());
        file << content;
        file.close();
    }
}

// ========== 测试用例 ==========

int main() {
    std::vector<mini_test::TestCase> tests;
    
    // 创建临时测试目录
    fs::path testDir = createTempTestDir();
    
    // 确保测试结束时清理
    struct Cleanup {
        fs::path dir;
        ~Cleanup() { cleanupTempTestDir(dir); }
    } cleanup{testDir};
    
    // ========== 工具注册测试 ==========
    
    tests.push_back({"CodeTools_RegisterAllTools", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        CHECK_TRUE(toolManager.hasTool("read_file"));
        CHECK_TRUE(toolManager.hasTool("write_file"));
        CHECK_TRUE(toolManager.hasTool("list_files"));
        CHECK_TRUE(toolManager.hasTool("search_code"));
        CHECK_TRUE(toolManager.hasTool("get_project_structure"));
        CHECK_TRUE(toolManager.hasTool("analyze_code"));
        
        CHECK_EQ(toolManager.getToolCount(), 6u);
    }});
    
    // ========== read_file 工具测试 ==========
    
    tests.push_back({"ReadFile_ReadCompleteFile", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "test.txt";
        createTestFile(testFile, "Line 1\nLine 2\nLine 3");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        
        auto result = toolManager.executeTool("read_file", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_TRUE(result->contains("content"));
        CHECK_TRUE(result->contains("path"));
        CHECK_TRUE(result->contains("line_count"));
        CHECK_EQ((*result)["line_count"].get<int>(), 3);
        CHECK_TRUE((*result)["content"].get<std::string>().find("Line 1") != std::string::npos);
    }});
    
    tests.push_back({"ReadFile_ReadLineRange", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "test.txt";
        createTestFile(testFile, "Line 1\nLine 2\nLine 3\nLine 4\nLine 5");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["start_line"] = 2;
        args["end_line"] = 4;
        
        auto result = toolManager.executeTool("read_file", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_EQ((*result)["start_line"].get<int>(), 2);
        CHECK_EQ((*result)["end_line"].get<int>(), 4);
        std::string content = (*result)["content"].get<std::string>();
        CHECK_TRUE(content.find("Line 2") != std::string::npos);
        CHECK_TRUE(content.find("Line 4") != std::string::npos);
    }});
    
    tests.push_back({"ReadFile_FileNotFound", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        nlohmann::json args;
        args["path"] = (testDir / "nonexistent.txt").string();
        
        auto result = toolManager.executeTool("read_file", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE(result->contains("error"));
    }});
    
    // ========== write_file 工具测试 ==========
    
    tests.push_back({"WriteFile_WriteNewFile", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "write_test.txt";
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["content"] = "Hello, World!";
        args["mode"] = "overwrite";
        
        auto result = toolManager.executeTool("write_file", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE((*result)["success"].get<bool>());
        CHECK_TRUE(fs::exists(testFile));
        
        // 验证文件内容
        std::ifstream file(testFile);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK_EQ(content, "Hello, World!");
    }});
    
    tests.push_back({"WriteFile_AppendMode", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "append_test.txt";
        createTestFile(testFile, "Original\n");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["content"] = "Appended";
        args["mode"] = "append";
        
        auto result = toolManager.executeTool("write_file", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE((*result)["success"].get<bool>());
        
        // 验证文件内容
        std::ifstream file(testFile);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK_TRUE(content.find("Original") != std::string::npos);
        CHECK_TRUE(content.find("Appended") != std::string::npos);
    }});
    
    tests.push_back({"WriteFile_CreateDirectories", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "subdir" / "nested" / "test.txt";
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["content"] = "Nested file";
        args["create_directories"] = true;
        
        auto result = toolManager.executeTool("write_file", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE((*result)["success"].get<bool>());
        CHECK_TRUE(fs::exists(testFile));
    }});
    
    // ========== list_files 工具测试 ==========
    
    tests.push_back({"ListFiles_ListDirectory", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        // 创建测试文件
        createTestFile(testDir / "file1.txt", "content1");
        createTestFile(testDir / "file2.cpp", "content2");
        fs::create_directories(testDir / "subdir");
        
        nlohmann::json args;
        args["directory"] = testDir.string();
        
        auto result = toolManager.executeTool("list_files", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_TRUE(result->contains("files"));
        CHECK_TRUE(result->contains("count"));
        CHECK_TRUE((*result)["count"].get<int>() >= 2);
    }});
    
    tests.push_back({"ListFiles_WithPattern", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        createTestFile(testDir / "file1.txt", "content1");
        createTestFile(testDir / "file2.cpp", "content2");
        createTestFile(testDir / "file3.cpp", "content3");
        
        nlohmann::json args;
        args["directory"] = testDir.string();
        args["pattern"] = "*.cpp";
        
        auto result = toolManager.executeTool("list_files", args);
        CHECK_TRUE(result.has_value());
        
        auto files = (*result)["files"].get<std::vector<std::string>>();
        for (const auto& file : files) {
            CHECK_TRUE(file.find(".cpp") != std::string::npos);
        }
    }});
    
    tests.push_back({"ListFiles_Recursive", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path subdir = testDir / "subdir";
        fs::create_directories(subdir);
        createTestFile(subdir / "nested.txt", "nested");
        
        nlohmann::json args;
        args["directory"] = testDir.string();
        args["recursive"] = true;
        
        auto result = toolManager.executeTool("list_files", args);
        CHECK_TRUE(result.has_value());
        
        auto files = (*result)["files"].get<std::vector<std::string>>();
        bool foundNested = false;
        for (const auto& file : files) {
            if (file.find("nested.txt") != std::string::npos) {
                foundNested = true;
                break;
            }
        }
        CHECK_TRUE(foundNested);
    }});
    
    // ========== search_code 工具测试 ==========
    
    tests.push_back({"SearchCode_TextSearch", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        createTestFile(testDir / "test.cpp", "int main() {\n    return 0;\n}");
        
        nlohmann::json args;
        args["query"] = "main";
        args["directory"] = testDir.string();
        
        auto result = toolManager.executeTool("search_code", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_TRUE(result->contains("matches"));
        CHECK_TRUE(result->contains("total_matches"));
        CHECK_TRUE((*result)["total_matches"].get<int>() > 0);
    }});
    
    tests.push_back({"SearchCode_RegexSearch", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        createTestFile(testDir / "test.cpp", "int func1() {}\nint func2() {}");
        
        nlohmann::json args;
        args["query"] = "func\\d+";
        args["directory"] = testDir.string();
        
        auto result = toolManager.executeTool("search_code", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_TRUE((*result)["total_matches"].get<int>() >= 2);
    }});
    
    tests.push_back({"SearchCode_FilePattern", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        createTestFile(testDir / "test.cpp", "int main() {}");
        createTestFile(testDir / "test.txt", "int main() {}");
        
        nlohmann::json args;
        args["query"] = "main";
        args["directory"] = testDir.string();
        args["file_pattern"] = "*.cpp";
        
        auto result = toolManager.executeTool("search_code", args);
        CHECK_TRUE(result.has_value());
        
        auto matches = (*result)["matches"].get<std::vector<nlohmann::json>>();
        for (const auto& match : matches) {
            std::string file = match["file"].get<std::string>();
            CHECK_TRUE(file.find(".cpp") != std::string::npos);
        }
    }});
    
    // ========== get_project_structure 工具测试 ==========
    
    tests.push_back({"GetProjectStructure_Basic", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        // 创建简单的项目结构
        createTestFile(testDir / "CMakeLists.txt", "project(TestProject)\nadd_executable(test test.cpp)");
        createTestFile(testDir / "test.cpp", "int main() {}");
        createTestFile(testDir / "test.h", "#pragma once");
        
        nlohmann::json args;
        args["project_root"] = testDir.string();
        
        auto result = toolManager.executeTool("get_project_structure", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_TRUE(result->contains("root_path"));
        CHECK_TRUE(result->contains("source_files"));
        CHECK_TRUE(result->contains("header_files"));
    }});
    
    tests.push_back({"GetProjectStructure_CMakeParsing", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        createTestFile(testDir / "CMakeLists.txt", "project(MyProject)\nadd_executable(app main.cpp)");
        
        nlohmann::json args;
        args["project_root"] = testDir.string();
        args["include_dependencies"] = true;
        
        auto result = toolManager.executeTool("get_project_structure", args);
        CHECK_TRUE(result.has_value());
        
        if (result->contains("cmake_config")) {
            auto cmakeConfig = (*result)["cmake_config"];
            if (cmakeConfig.contains("project_name")) {
                std::string projectName = cmakeConfig["project_name"].get<std::string>();
                CHECK_TRUE(!projectName.empty());
            }
        }
    }});
    
    // ========== analyze_code 工具测试 ==========
    
    tests.push_back({"AnalyzeCode_CppFile", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "test.cpp";
        createTestFile(testFile, "#include <iostream>\nclass MyClass {\npublic:\n    void method() {}\n};\nvoid func() {}");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["analysis_type"] = "all";
        
        auto result = toolManager.executeTool("analyze_code", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_EQ((*result)["language"].get<std::string>(), "cpp");
        CHECK_TRUE(result->contains("functions"));
        CHECK_TRUE(result->contains("classes"));
        CHECK_TRUE(result->contains("includes"));
    }});
    
    tests.push_back({"AnalyzeCode_PythonFile", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "test.py";
        createTestFile(testFile, "import os\nclass MyClass:\n    def method(self):\n        pass\ndef func():\n    pass");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        args["analysis_type"] = "all";
        
        auto result = toolManager.executeTool("analyze_code", args);
        CHECK_TRUE(result.has_value());
        
        CHECK_EQ((*result)["language"].get<std::string>(), "python");
        CHECK_TRUE(result->contains("functions"));
        CHECK_TRUE(result->contains("classes"));
        CHECK_TRUE(result->contains("includes"));
    }});
    
    tests.push_back({"AnalyzeCode_UnsupportedFile", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        fs::path testFile = testDir / "test.txt";
        createTestFile(testFile, "some text");
        
        nlohmann::json args;
        args["path"] = testFile.string();
        
        auto result = toolManager.executeTool("analyze_code", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE(result->contains("error"));
    }});
    
    // ========== 错误处理测试 ==========
    
    tests.push_back({"ErrorHandling_InvalidArguments", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        nlohmann::json args;
        // 缺少必需参数 path
        args["invalid"] = "value";
        
        auto result = toolManager.executeTool("read_file", args);
        // ToolManager 会进行参数验证，可能返回 nullopt 或错误
        // 这里主要测试工具不会崩溃
    }});
    
    tests.push_back({"ErrorHandling_InvalidPath", [&]() {
        ToolManager toolManager;
        CodeTools::registerAllTools(toolManager);
        
        nlohmann::json args;
        args["path"] = "/nonexistent/path/file.txt";
        
        auto result = toolManager.executeTool("read_file", args);
        CHECK_TRUE(result.has_value());
        CHECK_TRUE(result->contains("error"));
    }});
    
    // 运行所有测试
    return mini_test::run(tests);
}

