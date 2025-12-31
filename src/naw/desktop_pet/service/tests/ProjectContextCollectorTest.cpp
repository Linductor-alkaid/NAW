#include "naw/desktop_pet/service/ProjectContextCollector.h"
#include "naw/desktop_pet/service/ErrorTypes.h"

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
        fs::path tempDir = fs::temp_directory_path() / "ProjectContextCollectorTest";
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
    
    // 创建测试CMakeLists.txt
    void createTestCMakeLists(const fs::path& dir) {
        std::ofstream file(dir / "CMakeLists.txt");
        file << "cmake_minimum_required(VERSION 3.10)\n";
        file << "project(TestProject)\n";
        file << "add_executable(main src/main.cpp)\n";
        file << "target_link_libraries(main boost json)\n";
        file << "find_package(OpenCV REQUIRED)\n";
        file << "target_compile_options(main PRIVATE -Wall -Wextra)\n";
        file << "target_include_directories(main PRIVATE include)\n";
        file.close();
    }
    
    // 创建测试源文件
    void createTestSourceFile(const fs::path& filePath, const std::string& content) {
        fs::create_directories(filePath.parent_path());
        std::ofstream file(filePath);
        file << content;
        file.close();
    }
}

// ========== 测试用例 ==========

int main() {
    std::vector<mini_test::TestCase> tests;
    
    // ========== detectProjectRoot 测试 ==========
    
    tests.push_back({"ProjectContextCollector_DetectProjectRoot", []() {
        fs::path tempDir = createTempTestDir();
        try {
            // 创建项目标识文件
            createTestCMakeLists(tempDir);
            
            // 测试从子目录检测
            fs::path subDir = tempDir / "src";
            fs::create_directories(subDir);
            
            std::string root = ProjectContextCollector::detectProjectRoot(subDir.string());
            CHECK_TRUE(fs::equivalent(fs::path(root), tempDir));
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== parseCMakeLists 测试 ==========
    
    tests.push_back({"ProjectContextCollector_ParseCMakeLists", []() {
        fs::path tempDir = createTempTestDir();
        try {
            createTestCMakeLists(tempDir);
            
            auto config = ProjectContextCollector::parseCMakeLists((tempDir / "CMakeLists.txt").string());
            
            CHECK_TRUE(config.contains("project_name"));
            CHECK_EQ(config["project_name"].get<std::string>(), "TestProject");
            
            CHECK_TRUE(config.contains("targets"));
            CHECK_TRUE(config["targets"].is_array());
            CHECK_TRUE(config["targets"].size() > 0);
            
            CHECK_TRUE(config.contains("dependencies"));
            CHECK_TRUE(config["dependencies"].is_array());
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== analyzeProject 测试 ==========
    
    tests.push_back({"ProjectContextCollector_AnalyzeProject", []() {
        fs::path tempDir = createTempTestDir();
        try {
            createTestCMakeLists(tempDir);
            
            // 创建测试源文件
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", "int main() { return 0; }\n");
            
            fs::path includeDir = tempDir / "include";
            fs::create_directories(includeDir);
            createTestSourceFile(includeDir / "header.h", "#pragma once\n");
            
            ProjectContextCollector collector;
            ErrorInfo error;
            ProjectInfo info = collector.analyzeProject(tempDir.string(), &error);
            
            CHECK_TRUE(!info.rootPath.empty());
            CHECK_TRUE(!info.name.empty());
            CHECK_TRUE(info.sourceFiles.size() > 0);
            CHECK_TRUE(info.headerFiles.size() > 0);
            CHECK_TRUE(!info.cmakeConfig.empty());
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== extractDependenciesFromCMake 测试 ==========
    
    tests.push_back({"ProjectContextCollector_ExtractDependenciesFromCMake", []() {
        nlohmann::json cmakeConfig;
        cmakeConfig["dependencies"] = nlohmann::json::array();
        cmakeConfig["dependencies"].push_back("boost");
        cmakeConfig["dependencies"].push_back("json");
        cmakeConfig["dependencies"].push_back("OpenCV");
        
        auto deps = ProjectContextCollector::extractDependenciesFromCMake(cmakeConfig);
        
        CHECK_TRUE(deps.size() >= 3);
        CHECK_TRUE(std::find(deps.begin(), deps.end(), "boost") != deps.end());
        CHECK_TRUE(std::find(deps.begin(), deps.end(), "json") != deps.end());
        
    }});
    
    // ========== extractIncludesFromSource 测试 ==========
    
    tests.push_back({"ProjectContextCollector_ExtractIncludesFromSource", []() {
        fs::path tempDir = createTempTestDir();
        try {
            // 创建项目结构
            fs::path includeDir = tempDir / "include";
            fs::create_directories(includeDir);
            createTestSourceFile(includeDir / "header.h", "#pragma once\n");
            
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", 
                "#include \"header.h\"\n"
                "#include <iostream>\n"
                "int main() { return 0; }\n"
            );
            
            ProjectInfo projectInfo;
            projectInfo.rootPath = tempDir.string();
            projectInfo.headerFiles.push_back((includeDir / "header.h").string());
            
            auto includes = ProjectContextCollector::extractIncludesFromSource(
                (srcDir / "main.cpp").string(),
                projectInfo
            );
            
            CHECK_TRUE(includes.size() > 0);
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== findRelatedFiles 测试 ==========
    
    tests.push_back({"ProjectContextCollector_FindRelatedFiles", []() {
        fs::path tempDir = createTempTestDir();
        try {
            // 创建项目结构
            fs::path includeDir = tempDir / "include";
            fs::create_directories(includeDir);
            createTestSourceFile(includeDir / "header.h", "#pragma once\n");
            
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", 
                "#include \"header.h\"\n"
                "int main() { return 0; }\n"
            );
            
            ProjectInfo projectInfo;
            projectInfo.rootPath = tempDir.string();
            projectInfo.sourceFiles.push_back((srcDir / "main.cpp").string());
            projectInfo.headerFiles.push_back((includeDir / "header.h").string());
            
            auto relatedFiles = ProjectContextCollector::findRelatedFiles(
                (includeDir / "header.h").string(),
                projectInfo
            );
            
            // 应该找到包含该头文件的源文件
            CHECK_TRUE(relatedFiles.size() > 0);
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== getFileContext 测试 ==========
    
    tests.push_back({"ProjectContextCollector_GetFileContext", []() {
        fs::path tempDir = createTempTestDir();
        try {
            // 创建项目结构
            fs::path includeDir = tempDir / "include";
            fs::create_directories(includeDir);
            createTestSourceFile(includeDir / "header.h", "#pragma once\nclass Test {};\n");
            
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", 
                "#include \"header.h\"\n"
                "int main() { return 0; }\n"
            );
            
            ProjectInfo projectInfo;
            projectInfo.rootPath = tempDir.string();
            projectInfo.sourceFiles.push_back((srcDir / "main.cpp").string());
            projectInfo.headerFiles.push_back((includeDir / "header.h").string());
            
            ProjectContextCollector collector;
            std::string context = collector.getFileContext(
                (srcDir / "main.cpp").string(),
                projectInfo,
                1,  // maxDepth
                10, // maxFiles
                0   // maxTokens (不限制)
            );
            
            CHECK_TRUE(!context.empty());
            CHECK_TRUE(context.find("main.cpp") != std::string::npos);
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== getProjectSummary 测试 ==========
    
    tests.push_back({"ProjectContextCollector_GetProjectSummary", []() {
        fs::path tempDir = createTempTestDir();
        try {
            createTestCMakeLists(tempDir);
            
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", "int main() { return 0; }\n");
            
            ProjectContextCollector collector;
            ErrorInfo error;
            ProjectInfo info = collector.analyzeProject(tempDir.string(), &error);
            
            std::string summary = collector.getProjectSummary(info);
            
            CHECK_TRUE(!summary.empty());
            CHECK_TRUE(summary.find("Project:") != std::string::npos || 
                      summary.find("Project") != std::string::npos);
            
            // 测试缓存
            std::string summary2 = collector.getProjectSummary(info);
            CHECK_EQ(summary, summary2); // 应该返回相同的缓存结果
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== collectProjectContext 测试 ==========
    
    tests.push_back({"ProjectContextCollector_CollectProjectContext", []() {
        fs::path tempDir = createTempTestDir();
        try {
            createTestCMakeLists(tempDir);
            
            fs::path srcDir = tempDir / "src";
            fs::create_directories(srcDir);
            createTestSourceFile(srcDir / "main.cpp", "int main() { return 0; }\n");
            
            ProjectContextCollector collector;
            ErrorInfo error;
            ProjectContext context = collector.collectProjectContext(tempDir.string(), &error);
            
            CHECK_TRUE(!context.projectRoot.empty());
            CHECK_TRUE(!context.structureSummary.empty());
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== 缓存管理测试 ==========
    
    tests.push_back({"ProjectContextCollector_CacheManagement", []() {
        fs::path tempDir = createTempTestDir();
        try {
            createTestCMakeLists(tempDir);
            createTestSourceFile(tempDir / "test.cpp", "int test() { return 0; }\n");
            
            ProjectContextCollector collector;
            
            // 读取文件（应该缓存）
            ProjectInfo info;
            info.rootPath = tempDir.string();
            std::string content1 = collector.getFileContext(
                (tempDir / "test.cpp").string(),
                info
            );
            
            // 再次读取（应该从缓存获取）
            std::string content2 = collector.getFileContext(
                (tempDir / "test.cpp").string(),
                info
            );
            
            CHECK_EQ(content1, content2);
            
            // 清除缓存
            collector.clearFileCache();
            
        } catch (...) {
            cleanupTempTestDir(tempDir);
            throw;
        }
        cleanupTempTestDir(tempDir);
    }});
    
    // ========== 文件类型识别测试 ==========
    
    tests.push_back({"ProjectContextCollector_IdentifyFileType", []() {
        CHECK_EQ(ProjectContextCollector::identifyFileType("test.cpp"), "cpp");
        CHECK_EQ(ProjectContextCollector::identifyFileType("test.h"), "header");
        CHECK_EQ(ProjectContextCollector::identifyFileType("test.py"), "python");
        CHECK_EQ(ProjectContextCollector::identifyFileType("CMakeLists.txt"), "cmake");
        CHECK_EQ(ProjectContextCollector::identifyFileType("config.json"), "config");
        CHECK_EQ(ProjectContextCollector::identifyFileType("unknown.xyz"), "other");
    }});
    
    // 运行所有测试
    return mini_test::run(tests);
}

