#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

namespace {
    /**
     * @brief 默认构建目录名称列表
     */
    const std::vector<std::string> DEFAULT_BUILD_DIRS = {
        "build", "build-*", "cmake-build-*", "cmake-build",
        ".vs", ".vscode", ".idea", "out", "bin", "lib",
        "Debug", "Release", "x64", "x86", "obj", "target",
        "dist", ".gradle", ".mvn", "node_modules", ".pytest_cache",
        "__pycache__", ".cache", ".build"
    };
    
    /**
     * @brief 默认排除的文件扩展名
     */
    const std::vector<std::string> DEFAULT_EXCLUDED_EXTS = {
        ".o", ".obj", ".exe", ".dll", ".so", ".a", ".lib", ".pdb",
        ".tmp", ".bak", ".swp", ".swo", ".log", ".cache",
        ".class", ".pyc", ".pyo", ".egg-info"
    };
    
    /**
     * @brief 默认版本控制目录
     */
    const std::vector<std::string> DEFAULT_VCS_DIRS = {
        ".git", ".svn", ".hg", ".bzr", ".cvs"
    };
    
    /**
     * @brief 检查目录名是否匹配模式（支持通配符）
     */
    bool matchesDirPattern(const std::string& dirName, const std::string& pattern) {
        if (pattern.empty()) return false;
        
        // 支持通配符 * 和 ?
        std::string regexPattern = wildcardToRegex(pattern);
        try {
            std::regex re(regexPattern, std::regex::icase);
            return std::regex_match(dirName, re);
        } catch (...) {
            return false;
        }
    }
    
    /**
     * @brief 检查路径是否应该被排除
     * @param path 要检查的路径
     * @param projectRoot 项目根目录
     * @param excludePatterns 用户自定义排除模式
     * @param includePatterns 用户自定义包含模式（优先级最高）
     */
    bool shouldExcludePath(const fs::path& path, const fs::path& projectRoot,
                          const std::vector<std::string>& excludePatterns,
                          const std::vector<std::string>& includePatterns) {
        try {
            // 计算相对路径
            fs::path relPath = fs::relative(path, projectRoot);
            if (relPath.empty() || relPath == ".") {
                return false; // 不排除根目录
            }
            
            // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
            std::string pathStr = pathToUtf8String(relPath);
            // 统一使用正斜杠作为路径分隔符（跨平台兼容）
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            std::string filename = pathToUtf8String(path.filename());
            
            // 1. 检查用户自定义包含模式（最高优先级）
            for (const auto& pattern : includePatterns) {
                std::string normalizedPattern = pattern;
                std::replace(normalizedPattern.begin(), normalizedPattern.end(), '\\', '/');
                if (matchesPattern(pathStr, normalizedPattern) || matchesPattern(filename, normalizedPattern)) {
                    return false; // 包含模式匹配，不排除
                }
            }
            
            // 2. 检查用户自定义排除模式
            for (const auto& pattern : excludePatterns) {
                std::string normalizedPattern = pattern;
                std::replace(normalizedPattern.begin(), normalizedPattern.end(), '\\', '/');
                if (matchesPattern(pathStr, normalizedPattern) || matchesPattern(filename, normalizedPattern)) {
                    return true; // 匹配排除模式
                }
            }
            
            // 3. 检查版本控制目录
            for (const auto& vcsDir : DEFAULT_VCS_DIRS) {
                std::string vcsDirWithSep = vcsDir + std::string(1, fs::path::preferred_separator);
                if (pathStr.find(vcsDirWithSep) == 0 ||
                    pathStr == vcsDir ||
                    filename == vcsDir) {
                    return true;
                }
            }
            
            // 4. 检查构建目录（检查路径中的每个目录组件）
            std::vector<fs::path> pathComponents;
            for (const auto& component : relPath) {
                // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
                std::string compStr = pathToUtf8String(component);
                for (const auto& buildDir : DEFAULT_BUILD_DIRS) {
                    if (matchesDirPattern(compStr, buildDir)) {
                        return true;
                    }
                }
            }
            
            // 5. 检查文件扩展名（仅对文件）
            if (fs::is_regular_file(path)) {
                // 扩展名通常不会有编码问题，但为了安全也使用 pathToUtf8String()
                std::string ext = pathToUtf8String(path.extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                for (const auto& excludedExt : DEFAULT_EXCLUDED_EXTS) {
                    if (ext == excludedExt) {
                        return true;
                    }
                }
            }
            
            return false;
        } catch (...) {
            // 如果检查失败，默认不排除
            return false;
        }
    }
    
    /**
     * @brief 将路径转换为相对路径字符串（统一分隔符）
     */
    std::string convertToRelativePath(const fs::path& path, const fs::path& projectRoot) {
        try {
            // 确保使用绝对路径进行计算，以获得正确的相对路径
            fs::path absPath = fs::absolute(path);
            fs::path absRoot = fs::absolute(projectRoot);
            
            // 规范化路径（移除.和..）
            absPath = absPath.lexically_normal();
            absRoot = absRoot.lexically_normal();
            
            // 如果路径就是根目录本身，返回空字符串
            if (absPath == absRoot) {
                return "";
            }
            
            // 在 Windows 上，检查驱动器号是否一致
            #ifdef _WIN32
            fs::path rootPath = absRoot.root_path();
            fs::path fileRootPath = absPath.root_path();
            
            // 如果根路径不同（不同驱动器），无法计算相对路径
            if (rootPath != fileRootPath && !rootPath.empty() && !fileRootPath.empty()) {
                return "";
            }
            #endif
            
            // 优先尝试使用 lexically_relative（在 Windows 上更可靠）
            fs::path relPath = absPath.lexically_relative(absRoot);
            
            // 如果 lexically_relative 返回有效路径且没有 ".." 组件，使用它
            if (!relPath.empty() && relPath != ".") {
                // 检查是否有 ".." 组件（如果有，说明不在同一根下，不应该使用）
                bool hasParent = false;
                for (const auto& component : relPath) {
                    if (component == "..") {
                        hasParent = true;
                        break;
                    }
                }
                
                if (!hasParent) {
                    // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
                    std::string pathStr = pathToUtf8String(relPath);
                    // 统一使用正斜杠作为分隔符（跨平台兼容）
                    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                    return pathStr;
                }
            }
            
            // 如果 lexically_relative 失败，尝试 fs::relative
            try {
                relPath = fs::relative(absPath, absRoot);
                if (!relPath.empty() && relPath != ".") {
                    // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
                    std::string pathStr = pathToUtf8String(relPath);
                    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                    return pathStr;
                }
            } catch (const std::filesystem::filesystem_error&) {
                // fs::relative 失败，继续尝试手动计算
            }
            
            // 如果以上方法都失败，尝试手动计算
            // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
            std::string absPathStr = pathToUtf8String(absPath);
            std::string absRootStr = pathToUtf8String(absRoot);
            
            // 统一路径分隔符进行比较
            std::replace(absPathStr.begin(), absPathStr.end(), '\\', '/');
            std::replace(absRootStr.begin(), absRootStr.end(), '\\', '/');
            
            // 在 Windows 上，路径比较应该是大小写不敏感的
            #ifdef _WIN32
            auto toLower = [](const std::string& s) {
                std::string result = s;
                std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                return result;
            };
            std::string absPathStrLower = toLower(absPathStr);
            std::string absRootStrLower = toLower(absRootStr);
            #else
            const std::string& absPathStrLower = absPathStr;
            const std::string& absRootStrLower = absRootStr;
            #endif
            
            // 确保根路径以 / 结尾（用于比较）
            std::string rootStrForCompare = absRootStrLower;
            if (!rootStrForCompare.empty() && rootStrForCompare.back() != '/') {
                rootStrForCompare += '/';
            }
            
            // 检查文件路径是否以根路径开头（大小写不敏感）
            if (absPathStrLower.size() >= rootStrForCompare.size() && 
                absPathStrLower.substr(0, rootStrForCompare.size()) == rootStrForCompare) {
                // 提取相对部分（使用原始大小写的路径）
                std::string rootStrOriginal = absRootStr;
                if (!rootStrOriginal.empty() && rootStrOriginal.back() != '/') {
                    rootStrOriginal += '/';
                }
                std::string relativePart = absPathStr.substr(rootStrOriginal.size());
                // 移除前导斜杠
                while (!relativePart.empty() && relativePart[0] == '/') {
                    relativePart = relativePart.substr(1);
                }
                return relativePart;
            }
            
            return "";
        } catch (const std::exception&) {
            // 所有方法都失败，返回空字符串
            return "";
        } catch (...) {
            // 其他异常，返回空字符串
            return "";
        }
    }
    
    /**
     * @brief 检测项目根目录
     */
    fs::path detectProjectRoot(const fs::path& startPath) {
        fs::path current = fs::absolute(startPath);
        
        // 如果路径是文件，获取其父目录
        if (fs::exists(current) && fs::is_regular_file(current)) {
            current = current.parent_path();
        }
        
        // 安全检查：防止访问系统关键目录
        #ifdef _WIN32
        // Windows: 防止访问 C:\Windows, C:\Program Files 等系统目录
        try {
            std::string currentStr = pathToUtf8String(current);
            std::transform(currentStr.begin(), currentStr.end(), currentStr.begin(), ::tolower);
            if (currentStr.find("c:\\windows") == 0 || 
                currentStr.find("c:\\program files") == 0 ||
                currentStr.find("c:\\programdata") == 0 ||
                currentStr.find("c:\\system32") == 0 ||
                currentStr.find("c:\\program files (x86)") == 0) {
                // 如果起始路径在系统目录中，直接返回起始路径，不继续向上查找
                return fs::absolute(startPath);
            }
        } catch (...) {
            // 如果检查失败，继续正常流程
        }
        #endif
        
        // 向上遍历查找项目标识（限制最大深度，避免无限循环）
        int maxDepth = 10; // 减少到10层，避免遍历过深
        int depth = 0;
        while (!current.empty() && current != current.root_path() && depth < maxDepth) {
            try {
                // 检查常见的项目标识文件
                if (fs::exists(current / ".git") ||
                    fs::exists(current / "CMakeLists.txt") ||
                    fs::exists(current / ".project") ||
                    fs::exists(current / "package.json") ||
                    fs::exists(current / "pyproject.toml") ||
                    fs::exists(current / "setup.py")) {
                    return current;
                }
            } catch (...) {
                // 如果检查文件存在时出错，继续向上查找
            }
            
            fs::path parent = current.parent_path();
            if (parent == current) {
                // 已经到达根目录
                break;
            }
            current = parent;
            depth++;
        }
        
        // 如果没找到，返回起始路径的绝对路径
        return fs::absolute(startPath);
    }
    
    /**
     * @brief 解析 CMakeLists.txt
     */
    nlohmann::json parseCMakeLists(const fs::path& cmakePath) {
        nlohmann::json result;
        result["project_name"] = "";
        result["targets"] = nlohmann::json::array();
        result["dependencies"] = nlohmann::json::array();
        
        if (!fs::exists(cmakePath) || !fs::is_regular_file(cmakePath)) {
            return result;
        }
        
        try {
            std::ifstream file(cmakePath, std::ios::in);
            if (!file.is_open()) {
                return result;
            }
            
            std::string line;
            std::regex projectRegex(R"(project\s*\(\s*(\w+))");
            std::regex addExecutableRegex(R"(add_executable\s*\(\s*(\w+))");
            std::regex addLibraryRegex(R"(add_library\s*\(\s*(\w+))");
            std::regex targetLinkLibrariesRegex(R"(target_link_libraries\s*\(\s*(\w+)\s+(.+)\))");
            std::regex findPackageRegex(R"(find_package\s*\(\s*(\w+))");
            
            while (std::getline(file, line)) {
                // 移除注释
                size_t commentPos = line.find('#');
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                std::smatch match;
                
                // 解析 project()
                if (std::regex_search(line, match, projectRegex)) {
                    result["project_name"] = match[1].str();
                }
                
                // 解析 add_executable()
                if (std::regex_search(line, match, addExecutableRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                // 解析 add_library()
                if (std::regex_search(line, match, addLibraryRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                // 解析 target_link_libraries()
                if (std::regex_search(line, match, targetLinkLibrariesRegex)) {
                    std::string deps = match[2].str();
                    // 简单分割依赖（实际应该更复杂）
                    std::istringstream iss(deps);
                    std::string dep;
                    while (iss >> dep) {
                        if (!dep.empty()) {
                            result["dependencies"].push_back(dep);
                        }
                    }
                }
                
                // 解析 find_package()
                if (std::regex_search(line, match, findPackageRegex)) {
                    result["dependencies"].push_back(match[1].str());
                }
            }
            
            file.close();
        } catch (...) {
            // 解析失败，返回空结果
        }
        
        return result;
    }
}

static nlohmann::json handleGetProjectStructure(const nlohmann::json& arguments) {
    try {
        // 提取参数
        bool includeFiles = true;
        if (arguments.contains("include_files") && arguments["include_files"].is_boolean()) {
            includeFiles = arguments["include_files"].get<bool>();
        }
        
        bool includeDependencies = true;
        if (arguments.contains("include_dependencies") && arguments["include_dependencies"].is_boolean()) {
            includeDependencies = arguments["include_dependencies"].get<bool>();
        }
        
        bool useRelativePaths = true;
        if (arguments.contains("use_relative_paths") && arguments["use_relative_paths"].is_boolean()) {
            useRelativePaths = arguments["use_relative_paths"].get<bool>();
        }
        
        std::string detailLevel = "normal";
        if (arguments.contains("detail_level") && arguments["detail_level"].is_string()) {
            detailLevel = arguments["detail_level"].get<std::string>();
        }
        
        size_t maxFiles = 500;
        if (arguments.contains("max_files") && arguments["max_files"].is_number_integer()) {
            int maxFilesInt = arguments["max_files"].get<int>();
            if (maxFilesInt > 0) {
                maxFiles = static_cast<size_t>(maxFilesInt);
            }
        }
        
        size_t maxOutputSize = 1024 * 1024; // 1MB
        if (arguments.contains("max_output_size") && arguments["max_output_size"].is_number_unsigned()) {
            maxOutputSize = arguments["max_output_size"].get<size_t>();
        }
        
        // 提取自定义过滤模式
        std::vector<std::string> excludePatterns;
        if (arguments.contains("exclude_patterns") && arguments["exclude_patterns"].is_array()) {
            for (const auto& pattern : arguments["exclude_patterns"]) {
                if (pattern.is_string()) {
                    excludePatterns.push_back(pattern.get<std::string>());
                }
            }
        }
        
        std::vector<std::string> includePatterns;
        if (arguments.contains("include_patterns") && arguments["include_patterns"].is_array()) {
            for (const auto& pattern : arguments["include_patterns"]) {
                if (pattern.is_string()) {
                    includePatterns.push_back(pattern.get<std::string>());
                }
            }
        }
        
        fs::path projectRoot;
        if (arguments.contains("project_root") && arguments["project_root"].is_string()) {
            projectRoot = fs::path(arguments["project_root"].get<std::string>());
        } else {
            // 自动检测
            projectRoot = detectProjectRoot(fs::current_path());
        }
        
        projectRoot = fs::absolute(projectRoot);
        
        if (!fs::exists(projectRoot)) {
            // 错误消息中的路径使用 pathToUtf8String() 确保 UTF-8 编码
            return nlohmann::json{{"error", "项目根目录不存在: " + pathToUtf8String(projectRoot)}};
        }
        
        // 构建结果
        nlohmann::json result;
        if (useRelativePaths) {
            // 对于根路径，使用 "." 或项目根目录名
            result["root_path"] = ".";
        } else {
            // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
            result["root_path"] = pathToUtf8String(projectRoot);
        }
        result["project_name"] = "";
        
        // 解析 CMakeLists.txt
        fs::path cmakePath = projectRoot / "CMakeLists.txt";
        nlohmann::json cmakeConfig = parseCMakeLists(cmakePath);
        if (includeDependencies) {
            result["cmake_config"] = cmakeConfig;
            if (!cmakeConfig["project_name"].empty()) {
                result["project_name"] = cmakeConfig["project_name"];
            }
        }
        
        // 收集文件列表和构建目录结构（合并为一次遍历以提高性能）
        std::vector<std::string> sourceFiles;
        std::vector<std::string> headerFiles;
        std::set<std::string> seenPaths; // 用于去重
        std::ostringstream structure;
        std::set<std::string> structurePaths; // 用于去重
        size_t filesFiltered = 0;
        size_t filesSkipped = 0;
        
        // 安全限制：防止遍历过深或过多文件导致系统崩溃
        const size_t MAX_DEPTH = 10; // 最大递归深度（进一步减少以防止系统崩溃）
        const size_t MAX_ITERATIONS = 3000; // 最大迭代次数（进一步减少以防止系统崩溃）
        const size_t MAX_PATHS = (detailLevel == "minimal") ? 100 : 
                                (detailLevel == "normal") ? 300 : 500; // 减少路径限制
        const size_t MAX_STRUCTURE_SIZE = 100 * 1024; // 限制结构字符串最大为100KB
        
        if (includeFiles || detailLevel != "minimal") {
            try {
                fs::recursive_directory_iterator dirIter(projectRoot, 
                    fs::directory_options::skip_permission_denied);
                
                size_t iterationCount = 0;
                size_t pathCount = 0;
                
                for (const auto& entry : dirIter) {
                    // 安全检查：防止无限循环或过度遍历
                    if (++iterationCount > MAX_ITERATIONS) {
                        result["warning"] = "达到最大迭代次数限制(" + std::to_string(MAX_ITERATIONS) + ")，遍历已提前终止";
                        break;
                    }
                    
                    try {
                        // 计算当前深度（通过计算路径分隔符数量）
                        try {
                            fs::path relPath = fs::relative(entry.path(), projectRoot);
                            if (!relPath.empty() && relPath != ".") {
                                size_t depth = 0;
                                for (const auto& component : relPath) {
                                    if (component != "." && component != "..") {
                                        depth++;
                                    }
                                }
                                if (depth > MAX_DEPTH) {
                                    dirIter.disable_recursion_pending();
                                    continue;
                                }
                            }
                        } catch (...) {
                            // 如果计算深度失败，跳过这个条目
                            continue;
                        }
                        
                        // 跳过符号链接，避免无限循环
                        if (fs::is_symlink(entry)) {
                            dirIter.disable_recursion_pending();
                            continue;
                        }
                        
                        // 检查是否应该排除
                        if (shouldExcludePath(entry.path(), projectRoot, excludePatterns, includePatterns)) {
                            filesFiltered++;
                            // 如果是目录，禁用递归
                            if (fs::is_directory(entry)) {
                                dirIter.disable_recursion_pending();
                            }
                            continue;
                        }
                        
                        // 构建目录结构（如果启用）
                        if (pathCount < MAX_PATHS && structure.str().size() < MAX_STRUCTURE_SIZE) {
                            std::string relPathStr = convertToRelativePath(entry.path(), projectRoot);
                            if (!relPathStr.empty() && structurePaths.find(relPathStr) == structurePaths.end()) {
                                structurePaths.insert(relPathStr);
                                
                                // 根据详细度决定是否包含文件
                                if (detailLevel != "minimal" || fs::is_directory(entry)) {
                                    // 检查添加后是否会超过大小限制
                                    size_t newSize = structure.str().size() + relPathStr.size() + 2; // +2 for "/\n"
                                    if (newSize < MAX_STRUCTURE_SIZE) {
                                        structure << relPathStr;
                                        if (fs::is_directory(entry)) {
                                            structure << "/";
                                        }
                                        structure << "\n";
                                        pathCount++;
                                    } else {
                                        // 达到大小限制，停止添加
                                        break;
                                    }
                                }
                            }
                        } else if (pathCount >= MAX_PATHS || structure.str().size() >= MAX_STRUCTURE_SIZE) {
                            // 达到限制，可以提前退出（如果文件列表也已收集足够）
                            if (!includeFiles || (sourceFiles.size() + headerFiles.size() >= maxFiles)) {
                                break;
                            }
                        }
                        
                        // 收集文件列表（如果启用且未达到限制）
                        if (includeFiles && fs::is_regular_file(entry)) {
                            // 检查文件数量限制
                            size_t currentCount = sourceFiles.size() + headerFiles.size();
                            if (currentCount >= maxFiles) {
                                filesSkipped++;
                                // 达到文件限制后，如果也达到路径限制，可以提前退出
                                if (pathCount >= MAX_PATHS) {
                                    break;
                                }
                                continue;
                            }
                            
                            // 扩展名通常不会有编码问题，但为了安全也使用 pathToUtf8String()
                            std::string ext = pathToUtf8String(entry.path().extension());
                            
                            // 只处理 C++ 文件
                            bool isCppSource = (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c");
                            bool isCppHeader = (ext == ".h" || ext == ".hpp" || ext == ".hxx");
                            
                            if (!isCppSource && !isCppHeader) {
                                continue; // 跳过非 C++ 文件
                            }
                            
                            std::string pathStr;
                            if (useRelativePaths) {
                                pathStr = convertToRelativePath(entry.path(), projectRoot);
                            } else {
                                // 使用 pathToUtf8String() 确保返回 UTF-8 编码的字符串
                                pathStr = pathToUtf8String(entry.path());
                            }
                            
                            // 如果路径为空，跳过
                            if (pathStr.empty()) {
                                continue;
                            }
                            
                            // 去重检查
                            if (seenPaths.find(pathStr) != seenPaths.end()) {
                                continue;
                            }
                            
                            // 标记为已处理（在添加到列表之前）
                            seenPaths.insert(pathStr);
                            
                            // C++ 源文件
                            if (isCppSource) {
                                sourceFiles.push_back(pathStr);
                            }
                            // C++ 头文件
                            else if (isCppHeader) {
                                headerFiles.push_back(pathStr);
                            }
                        }
                    } catch (const fs::filesystem_error&) {
                        // 跳过权限错误等文件系统错误
                        dirIter.disable_recursion_pending();
                        continue;
                    } catch (...) {
                        // 跳过其他异常
                        continue;
                    }
                }
            } catch (const std::exception& e) {
                result["warning"] = std::string("遍历目录时发生错误: ") + e.what();
            } catch (...) {
                result["warning"] = "遍历目录时发生未知错误";
            }
        }
        
        result["source_files"] = sourceFiles;
        result["header_files"] = headerFiles;
        result["files_filtered"] = filesFiltered;
        result["files_skipped"] = filesSkipped;
        result["structure"] = structure.str();
        
        // 提取依赖
        if (includeDependencies && cmakeConfig.contains("dependencies")) {
            result["dependencies"] = cmakeConfig["dependencies"];
        } else {
            result["dependencies"] = nlohmann::json::array();
        }
        
        // 检查输出大小并截断（如果需要）
        // 优化：先估算大小，避免对大结果进行完整序列化
        // 估算方法：基础JSON大小 + 文件路径平均长度 * 文件数量
        size_t estimatedSize = 500; // 基础JSON结构大小
        estimatedSize += result["structure"].get<std::string>().size();
        if (result.contains("cmake_config")) {
            estimatedSize += 200; // CMake配置估算
        }
        // 估算文件列表大小（每个路径平均50字符）
        estimatedSize += (sourceFiles.size() + headerFiles.size()) * 50;
        
        // 如果估算大小超过限制，提前截断文件列表
        if (estimatedSize > maxOutputSize) {
            size_t targetFiles = static_cast<size_t>((maxOutputSize - 500 - result["structure"].get<std::string>().size() - 200) / 50);
            if (targetFiles < sourceFiles.size() + headerFiles.size()) {
                // 按比例截断
                size_t sourceTarget = (sourceFiles.size() * targetFiles) / (sourceFiles.size() + headerFiles.size() + 1);
                size_t headerTarget = targetFiles - sourceTarget;
                if (sourceTarget < sourceFiles.size()) {
                    sourceFiles.resize(sourceTarget);
                }
                if (headerTarget < headerFiles.size()) {
                    headerFiles.resize(headerTarget);
                }
                result["source_files"] = sourceFiles;
                result["header_files"] = headerFiles;
                result["truncated"] = true;
            }
        }
        
        // 只在必要时进行完整序列化（限制最大尝试次数，避免无限循环）
        std::string jsonStr;
        size_t dumpAttempts = 0;
        const size_t MAX_DUMP_ATTEMPTS = 10; // 最多尝试10次
        
        try {
            jsonStr = result.dump();
            if (jsonStr.size() > maxOutputSize) {
                // 如果输出过大，减少文件列表（批量减少，避免多次序列化）
                size_t targetSize = static_cast<size_t>(maxOutputSize * 0.8); // 保留80%的空间
                size_t reductionStep = std::max<size_t>(1, (sourceFiles.size() + headerFiles.size()) / 10); // 每次减少10%
                
                while (jsonStr.size() > targetSize && 
                       (!sourceFiles.empty() || !headerFiles.empty()) &&
                       dumpAttempts < MAX_DUMP_ATTEMPTS) {
                    dumpAttempts++;
                    
                    // 批量减少文件
                    for (size_t i = 0; i < reductionStep && !sourceFiles.empty(); ++i) {
                        sourceFiles.pop_back();
                    }
                    for (size_t i = 0; i < reductionStep && !headerFiles.empty(); ++i) {
                        headerFiles.pop_back();
                    }
                    
                    result["source_files"] = sourceFiles;
                    result["header_files"] = headerFiles;
                    
                    // 重新序列化（但限制尝试次数）
                    try {
                        jsonStr = result.dump();
                    } catch (const std::exception&) {
                        // 如果序列化失败，停止尝试
                        break;
                    }
                }
                result["truncated"] = true;
            } else {
                result["truncated"] = false;
            }
            result["output_size"] = jsonStr.size();
        } catch (const std::bad_alloc&) {
            // 内存不足，强制截断
            result["source_files"] = nlohmann::json::array();
            result["header_files"] = nlohmann::json::array();
            result["truncated"] = true;
            result["warning"] = "输出过大，已强制截断文件列表以避免内存问题";
            try {
                jsonStr = result.dump();
                result["output_size"] = jsonStr.size();
            } catch (...) {
                result["output_size"] = 0;
            }
        } catch (const std::exception& e) {
            // 序列化失败，但至少返回部分结果
            result["truncated"] = true;
            result["warning"] = std::string("序列化输出时发生错误: ") + e.what();
            result["output_size"] = 0;
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("获取项目结构失败: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "获取项目结构时发生未知错误"}};
    }
}

void CodeTools::registerGetProjectStructureTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "get_project_structure";
    tool.description = "分析项目结构，包括目录结构、源文件列表、CMake配置和依赖关系。支持智能过滤和输出大小控制。";
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"include_files", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含文件列表"}
            }},
            {"include_dependencies", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含依赖关系"}
            }},
            {"project_root", {
                {"type", "string"},
                {"description", "项目根路径，默认自动检测"}
            }},
            {"use_relative_paths", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否使用相对路径（相对于项目根目录）"}
            }},
            {"detail_level", {
                {"type", "string"},
                {"enum", {"minimal", "normal", "full"}},
                {"default", "normal"},
                {"description", "详细度级别：minimal（仅目录）、normal（目录+重要文件）、full（所有文件）"}
            }},
            {"max_files", {
                {"type", "integer"},
                {"minimum", 1},
                {"default", 500},
                {"description", "最大文件数量限制"}
            }},
            {"max_output_size", {
                {"type", "integer"},
                {"minimum", 1024},
                {"default", 1048576},
                {"description", "最大输出大小（字节），默认1MB"}
            }},
            {"exclude_patterns", {
                {"type", "array"},
                {"items", {"type", "string"}},
                {"description", "自定义排除模式列表（支持通配符 * 和 ?）"}
            }},
            {"include_patterns", {
                {"type", "array"},
                {"items", {"type", "string"}},
                {"description", "自定义包含模式列表（优先级高于排除模式，支持通配符 * 和 ?）"}
            }}
        }}
    };
    tool.handler = handleGetProjectStructure;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}
