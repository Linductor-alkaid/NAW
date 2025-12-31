#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

namespace {
    /**
     * @brief 分析 C++ 代码
     */
    nlohmann::json analyzeCppCode(const fs::path& filePath, const std::string& analysisType) {
        nlohmann::json result;
        // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
        result["path"] = pathToUtf8String(filePath);
        result["language"] = "cpp";
        result["functions"] = nlohmann::json::array();
        result["classes"] = nlohmann::json::array();
        result["includes"] = nlohmann::json::array();
        
        try {
            std::ifstream file(filePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            int lineNumber = 0;
            
            // 正则表达式
            std::regex includeRegex(R"(#include\s*[<"]([^>"]+)[>"])");
            std::regex functionRegex(R"((?:(?:inline|static|constexpr|virtual|explicit)\s+)*\w+\s+\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
            std::regex classRegex(R"(class\s+(\w+)(?:\s*:\s*(?:public|private|protected)\s+\w+)?\s*\{)");
            std::regex methodRegex(R"((?:(?:inline|static|constexpr|virtual|explicit)\s+)*\w+\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:;|\{))");
            
            std::string currentClass;
            int currentClassLine = 0;
            std::vector<std::string> currentClassMethods;
            
            while (std::getline(file, line)) {
                lineNumber++;
                
                // 移除注释
                size_t commentPos = line.find("//");
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                // 提取 #include
                if (analysisType == "dependencies" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, includeRegex)) {
                        result["includes"].push_back(match[1].str());
                    }
                }
                
                // 提取类定义
                if (analysisType == "classes" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, classRegex)) {
                        // 保存之前的类
                        if (!currentClass.empty()) {
                            nlohmann::json classInfo;
                            classInfo["name"] = currentClass;
                            classInfo["line"] = currentClassLine;
                            classInfo["methods"] = currentClassMethods;
                            result["classes"].push_back(classInfo);
                        }
                        
                        currentClass = match[1].str();
                        currentClassLine = lineNumber;
                        currentClassMethods.clear();
                    }
                    
                    // 提取类方法
                    if (!currentClass.empty() && std::regex_search(line, match, methodRegex)) {
                        currentClassMethods.push_back(match[1].str());
                    }
                }
                
                // 提取函数定义（简单版本，不考虑命名空间等复杂情况）
                if (analysisType == "functions" || analysisType == "all") {
                    std::smatch match;
                    if (std::regex_search(line, match, functionRegex)) {
                        nlohmann::json funcInfo;
                        funcInfo["name"] = "function_" + std::to_string(lineNumber); // 简化处理
                        funcInfo["signature"] = line;
                        funcInfo["line"] = lineNumber;
                        result["functions"].push_back(funcInfo);
                    }
                }
            }
            
            // 保存最后一个类
            if (!currentClass.empty() && (analysisType == "classes" || analysisType == "all")) {
                nlohmann::json classInfo;
                classInfo["name"] = currentClass;
                classInfo["line"] = currentClassLine;
                classInfo["methods"] = currentClassMethods;
                result["classes"].push_back(classInfo);
            }
            
            file.close();
        } catch (...) {
            // 分析失败，返回已有结果
        }
        
        return result;
    }
    
    /**
     * @brief 分析 Python 代码
     */
    nlohmann::json analyzePythonCode(const fs::path& filePath, const std::string& analysisType) {
        nlohmann::json result;
        // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
        result["path"] = pathToUtf8String(filePath);
        result["language"] = "python";
        result["functions"] = nlohmann::json::array();
        result["classes"] = nlohmann::json::array();
        result["includes"] = nlohmann::json::array();
        
        try {
            std::ifstream file(filePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            int lineNumber = 0;
            
            std::regex importRegex(R"(import\s+(\w+)|from\s+(\w+)\s+import)");
            std::regex functionRegex(R"(def\s+(\w+)\s*\()");
            std::regex classRegex(R"(class\s+(\w+)(?:\([^)]+\))?\s*:)");
            
            std::string currentClass;
            int currentClassLine = 0;
            std::vector<std::string> currentClassMethods;
            
            while (std::getline(file, line)) {
                lineNumber++;
                
                // 移除注释
                size_t commentPos = line.find('#');
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                // 提取 import
                if (analysisType == "dependencies" || analysisType == "all") {
                    std::smatch importMatch;
                    if (std::regex_search(line, importMatch, importRegex)) {
                        std::string module = importMatch[1].str();
                        if (module.empty()) {
                            module = importMatch[2].str();
                        }
                        if (!module.empty()) {
                            result["includes"].push_back(module);
                        }
                    }
                }
                
                // 提取类定义
                if (analysisType == "classes" || analysisType == "all") {
                    std::smatch classMatch;
                    if (std::regex_search(line, classMatch, classRegex)) {
                        // 保存之前的类
                        if (!currentClass.empty()) {
                            nlohmann::json classInfo;
                            classInfo["name"] = currentClass;
                            classInfo["line"] = currentClassLine;
                            classInfo["methods"] = currentClassMethods;
                            result["classes"].push_back(classInfo);
                        }
                        
                        currentClass = classMatch[1].str();
                        currentClassLine = lineNumber;
                        currentClassMethods.clear();
                    }
                    
                    // 提取类方法（在类内部定义的函数）
                    if (!currentClass.empty()) {
                        std::smatch methodMatch;
                        if (std::regex_search(line, methodMatch, functionRegex)) {
                            currentClassMethods.push_back(methodMatch[1].str());
                        }
                    }
                }
                
                // 提取函数定义（不在类内部的）
                if (analysisType == "functions" || analysisType == "all") {
                    if (currentClass.empty()) {
                        std::smatch funcMatch;
                        if (std::regex_search(line, funcMatch, functionRegex)) {
                            nlohmann::json funcInfo;
                            funcInfo["name"] = funcMatch[1].str();
                            funcInfo["signature"] = line;
                            funcInfo["line"] = lineNumber;
                            result["functions"].push_back(funcInfo);
                        }
                    }
                }
            }
            
            // 保存最后一个类
            if (!currentClass.empty() && (analysisType == "classes" || analysisType == "all")) {
                nlohmann::json classInfo;
                classInfo["name"] = currentClass;
                classInfo["line"] = currentClassLine;
                classInfo["methods"] = currentClassMethods;
                result["classes"].push_back(classInfo);
            }
            
            file.close();
        } catch (...) {
            // 分析失败，返回已有结果
        }
        
        return result;
    }
}

static nlohmann::json handleAnalyzeCode(const nlohmann::json& arguments) {
    try {
        // 提取参数
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return nlohmann::json{{"error", "缺少必需参数: path"}};
        }
        
        std::string pathStr = arguments["path"].get<std::string>();
        fs::path filePath(pathStr);
        
        std::string analysisType = "all";
        if (arguments.contains("analysis_type") && arguments["analysis_type"].is_string()) {
            analysisType = arguments["analysis_type"].get<std::string>();
        }
        
        // 验证分析类型
        if (analysisType != "functions" && analysisType != "classes" && 
            analysisType != "dependencies" && analysisType != "all") {
            return nlohmann::json{{"error", "无效的分析类型: " + analysisType}};
        }
        
        // 检查文件是否存在
        if (!fs::exists(filePath)) {
            return nlohmann::json{{"error", "文件不存在: " + pathStr}};
        }
        
        if (!fs::is_regular_file(filePath)) {
            return nlohmann::json{{"error", "路径不是文件: " + pathStr}};
        }
        
        // 根据文件扩展名选择分析器
        // 扩展名通常不会有编码问题，但为了安全也使用 pathToUtf8String()
        std::string ext = pathToUtf8String(filePath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || 
            ext == ".h" || ext == ".hpp" || ext == ".hxx") {
            return analyzeCppCode(filePath, analysisType);
        } else if (ext == ".py") {
            return analyzePythonCode(filePath, analysisType);
        } else {
            return nlohmann::json{{"error", "不支持的文件类型: " + ext}};
        }
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("分析代码失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "分析代码时发生未知错误"}};
    }
}

void CodeTools::registerAnalyzeCodeTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "analyze_code";
    tool.description = "分析代码文件，提取函数定义、类定义和依赖关系。支持 C++ 和 Python。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "代码文件路径"}
            }},
            {"analysis_type", {
                {"type", "string"},
                {"enum", {"functions", "classes", "dependencies", "all"}},
                {"default", "all"},
                {"description", "分析类型"}
            }}
        }},
        {"required", {"path"}}
    };
    tool.handler = handleAnalyzeCode;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

