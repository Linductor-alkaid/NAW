#include "naw/desktop_pet/service/CodeTools.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"
#include "naw/desktop_pet/service/ToolManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service;
using namespace naw::desktop_pet::service::tools;

namespace {
    // ==================== 配置常量 ====================
    
    /**
     * @brief 安全限制配置
     */
    struct SafetyLimits {
        static constexpr size_t MAX_DEPTH = 8;              // 最大递归深度（降低以提高安全性）
        static constexpr size_t MAX_ITERATIONS = 2000;      // 最大迭代次数
        static constexpr size_t MAX_FILES = 500;            // 默认最大文件数
        static constexpr size_t MAX_STRUCTURE_SIZE = 80 * 1024;  // 结构字符串最大80KB
        static constexpr size_t MAX_OUTPUT_SIZE = 1024 * 1024;   // 输出最大1MB
        static constexpr size_t MAX_MEMORY_ESTIMATE = 50 * 1024 * 1024;  // 内存估算上限50MB
        static constexpr int TIMEOUT_SECONDS = 30;          // 超时时间30秒
        static constexpr size_t MIN_FREE_MEMORY = 100 * 1024 * 1024;  // 最小保留内存100MB
    };
    
    /**
     * @brief 默认构建目录名称列表
     */
    const std::vector<std::string> DEFAULT_BUILD_DIRS = {
        "build", "build-*", "cmake-build-*", "cmake-build",
        ".vs", ".vscode", ".idea", "out", "bin", "lib",
        "Debug", "Release", "x64", "x86", "obj", "target",
        "dist", ".gradle", ".mvn", "node_modules", ".pytest_cache",
        "__pycache__", ".cache", ".build", "vendor", "third_party"
    };
    
    /**
     * @brief 默认排除的文件扩展名
     */
    const std::vector<std::string> DEFAULT_EXCLUDED_EXTS = {
        ".o", ".obj", ".exe", ".dll", ".so", ".a", ".lib", ".pdb",
        ".tmp", ".bak", ".swp", ".swo", ".log", ".cache",
        ".class", ".pyc", ".pyo", ".egg-info", ".ilk", ".exp"
    };
    
    /**
     * @brief 默认版本控制目录
     */
    const std::vector<std::string> DEFAULT_VCS_DIRS = {
        ".git", ".svn", ".hg", ".bzr", ".cvs"
    };
    
    // ==================== 正则表达式缓存 ====================
    
    /**
     * @brief 正则表达式缓存（线程安全需要外部同步）
     */
    class RegexCache {
    private:
        std::unordered_map<std::string, std::regex> cache;
        size_t maxSize;
        
    public:
        explicit RegexCache(size_t max = 100) : maxSize(max) {}
        
        const std::regex* get(const std::string& pattern) {
            auto it = cache.find(pattern);
            if (it != cache.end()) {
                return &it->second;
            }
            
            if (cache.size() >= maxSize) {
                cache.clear(); // 简单的LRU替代：清空缓存
            }
            
            try {
                std::string regexPattern = wildcardToRegex(pattern);
                cache[pattern] = std::regex(regexPattern, std::regex::icase);
                return &cache[pattern];
            } catch (...) {
                return nullptr;
            }
        }
    };
    
    static RegexCache globalRegexCache;
    
    // ==================== 性能监控 ====================
    
    /**
     * @brief 性能统计信息
     */
    struct PerformanceStats {
        std::chrono::steady_clock::time_point startTime;
        size_t filesScanned = 0;
        size_t dirsScanned = 0;
        size_t filesFiltered = 0;
        size_t filesSkipped = 0;
        size_t memoryEstimate = 0;
        bool timedOut = false;
        bool memoryLimitHit = false;
        
        PerformanceStats() : startTime(std::chrono::steady_clock::now()) {}
        
        bool isTimeout(int maxSeconds) const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
            return elapsed.count() >= maxSeconds;
        }
        
        double getElapsedSeconds() const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
            return elapsed.count() / 1000.0;
        }
        
        void addMemoryEstimate(size_t size) {
            memoryEstimate += size;
            if (memoryEstimate > SafetyLimits::MAX_MEMORY_ESTIMATE) {
                memoryLimitHit = true;
            }
        }
    };
    
    // ==================== 路径处理优化 ====================
    
    /**
     * @brief 路径缓存（避免重复计算）
     */
    class PathCache {
    private:
        std::unordered_map<std::string, std::string> relativePathCache;
        fs::path projectRoot;
        size_t maxSize;
        
    public:
        explicit PathCache(const fs::path& root, size_t max = 500) 
            : projectRoot(root), maxSize(max) {}
        
        std::string getRelativePath(const fs::path& path) {
            std::string key = pathToUtf8String(path);
            
            auto it = relativePathCache.find(key);
            if (it != relativePathCache.end()) {
                return it->second;
            }
            
            if (relativePathCache.size() >= maxSize) {
                relativePathCache.clear();
            }
            
            std::string result = convertToRelativePathSafe(path, projectRoot);
            relativePathCache[key] = result;
            return result;
        }
        
    private:
        static std::string convertToRelativePathSafe(const fs::path& path, const fs::path& projectRoot) {
            try {
                fs::path absPath = fs::absolute(path);
                fs::path absRoot = fs::absolute(projectRoot);
                
                absPath = absPath.lexically_normal();
                absRoot = absRoot.lexically_normal();
                
                if (absPath == absRoot) {
                    return "";
                }
                
                #ifdef _WIN32
                fs::path rootPath = absRoot.root_path();
                fs::path fileRootPath = absPath.root_path();
                
                if (rootPath != fileRootPath && !rootPath.empty() && !fileRootPath.empty()) {
                    return "";
                }
                #endif
                
                fs::path relPath = absPath.lexically_relative(absRoot);
                
                if (!relPath.empty() && relPath != ".") {
                    bool hasParent = false;
                    for (const auto& component : relPath) {
                        if (component == "..") {
                            hasParent = true;
                            break;
                        }
                    }
                    
                    if (!hasParent) {
                        std::string pathStr = pathToUtf8String(relPath);
                        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                        return pathStr;
                    }
                }
                
                return "";
            } catch (...) {
                return "";
            }
        }
    };
    
    // ==================== 路径检查优化 ====================
    
    /**
     * @brief 检查目录名是否匹配模式（使用缓存的正则）
     */
    bool matchesDirPattern(const std::string& dirName, const std::string& pattern) {
        if (pattern.empty()) return false;
        
        const std::regex* re = globalRegexCache.get(pattern);
        if (!re) return false;
        
        try {
            return std::regex_match(dirName, *re);
        } catch (...) {
            return false;
        }
    }
    
    /**
     * @brief 检查是否为网络路径（Windows）
     */
    bool isNetworkPath(const fs::path& path) {
        #ifdef _WIN32
        try {
            std::string pathStr = pathToUtf8String(path);
            // UNC路径: \\server\share
            if (pathStr.size() >= 2 && pathStr[0] == '\\' && pathStr[1] == '\\') {
                return true;
            }
            // 映射的网络驱动器需要通过其他方式检测
        } catch (...) {}
        #endif
        return false;
    }
    
    /**
     * @brief 检查路径是否应该被排除
     */
    bool shouldExcludePath(const fs::path& path, const fs::path& projectRoot,
                          const std::vector<std::string>& excludePatterns,
                          const std::vector<std::string>& includePatterns,
                          PathCache& pathCache) {
        try {
            fs::path relPath = fs::relative(path, projectRoot);
            if (relPath.empty() || relPath == ".") {
                return false;
            }
            
            std::string pathStr = pathCache.getRelativePath(path);
            if (pathStr.empty()) {
                return false; // 无法计算相对路径，不排除
            }
            
            std::string filename = pathToUtf8String(path.filename());
            
            // 1. 检查用户自定义包含模式（最高优先级）
            for (const auto& pattern : includePatterns) {
                std::string normalizedPattern = pattern;
                std::replace(normalizedPattern.begin(), normalizedPattern.end(), '\\', '/');
                if (matchesPattern(pathStr, normalizedPattern) || matchesPattern(filename, normalizedPattern)) {
                    return false;
                }
            }
            
            // 2. 检查用户自定义排除模式
            for (const auto& pattern : excludePatterns) {
                std::string normalizedPattern = pattern;
                std::replace(normalizedPattern.begin(), normalizedPattern.end(), '\\', '/');
                if (matchesPattern(pathStr, normalizedPattern) || matchesPattern(filename, normalizedPattern)) {
                    return true;
                }
            }
            
            // 3. 检查版本控制目录
            for (const auto& vcsDir : DEFAULT_VCS_DIRS) {
                if (pathStr.find(vcsDir + "/") == 0 || pathStr == vcsDir || filename == vcsDir) {
                    return true;
                }
            }
            
            // 4. 检查构建目录
            for (const auto& component : relPath) {
                std::string compStr = pathToUtf8String(component);
                for (const auto& buildDir : DEFAULT_BUILD_DIRS) {
                    if (matchesDirPattern(compStr, buildDir)) {
                        return true;
                    }
                }
            }
            
            // 5. 检查文件扩展名（仅对文件）
            if (fs::is_regular_file(path)) {
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
            return false;
        }
    }
    
    // ==================== 项目根检测 ====================
    
    /**
     * @brief 检测项目根目录
     */
    fs::path detectProjectRoot(const fs::path& startPath) {
        fs::path current = fs::absolute(startPath);
        
        if (fs::exists(current) && fs::is_regular_file(current)) {
            current = current.parent_path();
        }
        
        // 安全检查：防止访问系统关键目录
        #ifdef _WIN32
        try {
            std::string currentStr = pathToUtf8String(current);
            std::transform(currentStr.begin(), currentStr.end(), currentStr.begin(), ::tolower);
            
            const std::vector<std::string> systemDirs = {
                "c:\\windows", "c:\\program files", "c:\\programdata", 
                "c:\\system32", "c:\\program files (x86)"
            };
            
            for (const auto& sysDir : systemDirs) {
                if (currentStr.find(sysDir) == 0) {
                    return fs::absolute(startPath);
                }
            }
        } catch (...) {}
        #endif
        
        // 向上遍历查找项目标识（限制最大深度）
        const std::vector<std::string> projectMarkers = {
            ".git", "CMakeLists.txt", ".project", "package.json",
            "pyproject.toml", "setup.py", "Cargo.toml", "go.mod",
            "pom.xml", "build.gradle", "Makefile"
        };
        
        int maxDepth = 8;
        int depth = 0;
        
        while (!current.empty() && current != current.root_path() && depth < maxDepth) {
            try {
                for (const auto& marker : projectMarkers) {
                    if (fs::exists(current / marker)) {
                        return current;
                    }
                }
            } catch (...) {}
            
            fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
            depth++;
        }
        
        return fs::absolute(startPath);
    }
    
    // ==================== CMake解析 ====================
    
    /**
     * @brief 解析CMakeLists.txt
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
            std::regex findPackageRegex(R"(find_package\s*\(\s*(\w+))");
            
            std::unordered_set<std::string> seenDeps;
            
            while (std::getline(file, line)) {
                size_t commentPos = line.find('#');
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                
                std::smatch match;
                
                if (std::regex_search(line, match, projectRegex)) {
                    result["project_name"] = match[1].str();
                }
                
                if (std::regex_search(line, match, addExecutableRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                if (std::regex_search(line, match, addLibraryRegex)) {
                    result["targets"].push_back(match[1].str());
                }
                
                if (std::regex_search(line, match, findPackageRegex)) {
                    std::string dep = match[1].str();
                    if (seenDeps.find(dep) == seenDeps.end()) {
                        seenDeps.insert(dep);
                        result["dependencies"].push_back(dep);
                    }
                }
            }
            
            file.close();
        } catch (...) {}
        
        return result;
    }
    
    // ==================== 主处理函数 ====================
    
    /**
     * @brief 扫描项目结构（核心函数）
     */
    nlohmann::json scanProjectStructure(
        const fs::path& projectRoot,
        bool includeFiles,
        bool useRelativePaths,
        const std::string& detailLevel,
        size_t maxFiles,
        const std::vector<std::string>& excludePatterns,
        const std::vector<std::string>& includePatterns,
        PerformanceStats& stats,
        PathCache& pathCache
    ) {
        nlohmann::json result;
        std::vector<std::string> sourceFiles;
        std::vector<std::string> headerFiles;
        std::unordered_set<std::string> seenPaths;
        std::ostringstream structure;
        std::set<std::string> structurePaths;
        
        // 安全限制
        const size_t MAX_PATHS = (detailLevel == "minimal") ? 100 : 
                                 (detailLevel == "normal") ? 300 : 500;
        
        try {
            fs::recursive_directory_iterator dirIter(projectRoot, 
                fs::directory_options::skip_permission_denied);
            
            size_t iterationCount = 0;
            size_t pathCount = 0;
            
            for (const auto& entry : dirIter) {
                // 超时检查
                if (stats.isTimeout(SafetyLimits::TIMEOUT_SECONDS)) {
                    stats.timedOut = true;
                    result["warning"] = "扫描超时(" + std::to_string(SafetyLimits::TIMEOUT_SECONDS) + 
                                       "秒)，已提前终止";
                    break;
                }
                
                // 迭代次数检查
                if (++iterationCount > SafetyLimits::MAX_ITERATIONS) {
                    result["warning"] = "达到最大迭代次数限制(" + 
                                       std::to_string(SafetyLimits::MAX_ITERATIONS) + ")，已提前终止";
                    break;
                }
                
                // 内存检查
                if (stats.memoryLimitHit) {
                    result["warning"] = "内存使用超限，已提前终止";
                    break;
                }
                
                try {
                    // 计算深度
                    try {
                        fs::path relPath = fs::relative(entry.path(), projectRoot);
                        if (!relPath.empty() && relPath != ".") {
                            size_t depth = 0;
                            for (const auto& component : relPath) {
                                if (component != "." && component != "..") {
                                    depth++;
                                }
                            }
                            if (depth > SafetyLimits::MAX_DEPTH) {
                                dirIter.disable_recursion_pending();
                                continue;
                            }
                        }
                    } catch (...) {
                        continue;
                    }
                    
                    // 跳过符号链接
                    if (fs::is_symlink(entry)) {
                        dirIter.disable_recursion_pending();
                        continue;
                    }
                    
                    // 检查是否应该排除
                    if (shouldExcludePath(entry.path(), projectRoot, excludePatterns, 
                                         includePatterns, pathCache)) {
                        stats.filesFiltered++;
                        if (fs::is_directory(entry)) {
                            dirIter.disable_recursion_pending();
                        }
                        continue;
                    }
                    
                    // 更新统计
                    if (fs::is_directory(entry)) {
                        stats.dirsScanned++;
                    } else if (fs::is_regular_file(entry)) {
                        stats.filesScanned++;
                    }
                    
                    // 构建目录结构
                    if (pathCount < MAX_PATHS && structure.str().size() < SafetyLimits::MAX_STRUCTURE_SIZE) {
                        std::string relPathStr = pathCache.getRelativePath(entry.path());
                        if (!relPathStr.empty() && structurePaths.find(relPathStr) == structurePaths.end()) {
                            structurePaths.insert(relPathStr);
                            
                            if (detailLevel != "minimal" || fs::is_directory(entry)) {
                                size_t newSize = structure.str().size() + relPathStr.size() + 2;
                                if (newSize < SafetyLimits::MAX_STRUCTURE_SIZE) {
                                    structure << relPathStr;
                                    if (fs::is_directory(entry)) {
                                        structure << "/";
                                    }
                                    structure << "\n";
                                    pathCount++;
                                    stats.addMemoryEstimate(relPathStr.size() + 2);
                                }
                            }
                        }
                    }
                    
                    // 收集文件列表
                    if (includeFiles && fs::is_regular_file(entry)) {
                        size_t currentCount = sourceFiles.size() + headerFiles.size();
                        if (currentCount >= maxFiles) {
                            stats.filesSkipped++;
                            continue;
                        }
                        
                        std::string ext = pathToUtf8String(entry.path().extension());
                        
                        bool isCppSource = (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c");
                        bool isCppHeader = (ext == ".h" || ext == ".hpp" || ext == ".hxx");
                        
                        if (!isCppSource && !isCppHeader) {
                            continue;
                        }
                        
                        std::string pathStr;
                        if (useRelativePaths) {
                            pathStr = pathCache.getRelativePath(entry.path());
                        } else {
                            pathStr = pathToUtf8String(entry.path());
                        }
                        
                        if (pathStr.empty() || seenPaths.find(pathStr) != seenPaths.end()) {
                            continue;
                        }
                        
                        seenPaths.insert(pathStr);
                        stats.addMemoryEstimate(pathStr.size());
                        
                        if (isCppSource) {
                            sourceFiles.push_back(pathStr);
                        } else if (isCppHeader) {
                            headerFiles.push_back(pathStr);
                        }
                    }
                    
                } catch (const fs::filesystem_error&) {
                    dirIter.disable_recursion_pending();
                    continue;
                } catch (...) {
                    continue;
                }
            }
        } catch (const std::exception& e) {
            result["warning"] = std::string("遍历目录时发生错误: ") + e.what();
        } catch (...) {
            result["warning"] = "遍历目录时发生未知错误";
        }
        
        result["source_files"] = sourceFiles;
        result["header_files"] = headerFiles;
        result["structure"] = structure.str();
        
        return result;
    }
}

// ==================== 工具处理函数 ====================

static nlohmann::json handleGetProjectStructure(const nlohmann::json& arguments) {
    try {
        // 提取参数
        bool includeFiles = arguments.value("include_files", true);
        bool includeDependencies = arguments.value("include_dependencies", true);
        bool useRelativePaths = arguments.value("use_relative_paths", true);
        std::string detailLevel = arguments.value("detail_level", "normal");
        size_t maxFiles = arguments.value("max_files", SafetyLimits::MAX_FILES);
        size_t maxOutputSize = arguments.value("max_output_size", SafetyLimits::MAX_OUTPUT_SIZE);
        
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
        
        // 确定项目根目录
        fs::path projectRoot;
        if (arguments.contains("project_root") && arguments["project_root"].is_string()) {
            projectRoot = fs::path(arguments["project_root"].get<std::string>());
        } else {
            projectRoot = detectProjectRoot(fs::current_path());
        }
        
        projectRoot = fs::absolute(projectRoot);
        
        // 验证项目根目录
        if (!fs::exists(projectRoot)) {
            return nlohmann::json{{"error", "项目根目录不存在: " + pathToUtf8String(projectRoot)}};
        }
        
        if (!fs::is_directory(projectRoot)) {
            return nlohmann::json{{"error", "项目根路径不是目录: " + pathToUtf8String(projectRoot)}};
        }
        
        // 检查网络路径
        if (isNetworkPath(projectRoot)) {
            return nlohmann::json{{"error", "不支持网络路径，请使用本地路径"}};
        }
        
        // 初始化结果
        nlohmann::json result;
        result["root_path"] = useRelativePaths ? "." : pathToUtf8String(projectRoot);
        result["project_name"] = "";
        
        // 解析CMakeLists.txt
        fs::path cmakePath = projectRoot / "CMakeLists.txt";
        nlohmann::json cmakeConfig = parseCMakeLists(cmakePath);
        if (includeDependencies) {
            result["cmake_config"] = cmakeConfig;
            if (!cmakeConfig["project_name"].empty()) {
                result["project_name"] = cmakeConfig["project_name"];
            }
        }
        
        // 初始化性能监控和缓存
        PerformanceStats stats;
        PathCache pathCache(projectRoot);
        
        // 扫描项目结构
        nlohmann::json scanResult = scanProjectStructure(
            projectRoot, includeFiles, useRelativePaths, detailLevel,
            maxFiles, excludePatterns, includePatterns, stats, pathCache
        );
        
        // 合并结果
        result["source_files"] = scanResult["source_files"];
        result["header_files"] = scanResult["header_files"];
        result["structure"] = scanResult["structure"];
        
        if (scanResult.contains("warning")) {
            result["warning"] = scanResult["warning"];
        }
        
        // 添加统计信息（在根级别添加关键统计字段以满足测试要求）
        result["files_filtered"] = stats.filesFiltered;
        result["files_skipped"] = stats.filesSkipped;
        
        result["stats"] = {
            {"files_scanned", stats.filesScanned},
            {"dirs_scanned", stats.dirsScanned},
            {"files_filtered", stats.filesFiltered},
            {"files_skipped", stats.filesSkipped},
            {"elapsed_seconds", stats.getElapsedSeconds()},
            {"timed_out", stats.timedOut},
            {"memory_limit_hit", stats.memoryLimitHit}
        };
        
        // 提取依赖
        if (includeDependencies && cmakeConfig.contains("dependencies")) {
            result["dependencies"] = cmakeConfig["dependencies"];
        } else {
            result["dependencies"] = nlohmann::json::array();
        }
        
        // 输出大小控制
        std::string jsonStr;
        try {
            jsonStr = result.dump();
            
            if (jsonStr.size() > maxOutputSize) {
                // 逐步减少文件列表
                auto& sourceFiles = result["source_files"];
                auto& headerFiles = result["header_files"];
                
                size_t reductionStep = std::max<size_t>(1, 
                    (sourceFiles.size() + headerFiles.size()) / 10);
                
                int attempts = 0;
                const int MAX_ATTEMPTS = 10;
                
                while (jsonStr.size() > maxOutputSize && attempts < MAX_ATTEMPTS) {
                    bool reduced = false;
                    
                    for (size_t i = 0; i < reductionStep && !sourceFiles.empty(); ++i) {
                        sourceFiles.erase(sourceFiles.size() - 1);
                        reduced = true;
                    }
                    
                    for (size_t i = 0; i < reductionStep && !headerFiles.empty(); ++i) {
                        headerFiles.erase(headerFiles.size() - 1);
                        reduced = true;
                    }
                    
                    if (!reduced) break;
                    
                    jsonStr = result.dump();
                    attempts++;
                }
                
                result["truncated"] = true;
                result["truncation_reason"] = "output_size_limit";
            } else {
                result["truncated"] = false;
            }
            
            result["output_size"] = jsonStr.size();
            
        } catch (const std::bad_alloc&) {
            result["source_files"] = nlohmann::json::array();
            result["header_files"] = nlohmann::json::array();
            result["structure"] = "";
            result["truncated"] = true;
            result["truncation_reason"] = "memory_allocation_failed";
            result["error"] = "内存不足，已清空文件列表";
            
            try {
                jsonStr = result.dump();
                result["output_size"] = jsonStr.size();
            } catch (...) {
                result["output_size"] = 0;
            }
        } catch (const std::exception& e) {
            result["truncated"] = true;
            result["truncation_reason"] = "serialization_error";
            result["warning"] = std::string("序列化输出时发生错误: ") + e.what();
            result["output_size"] = 0;
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{
            {"error", std::string("获取项目结构失败: ") + e.what()},
            {"success", false}
        };
    } catch (...) {
        return nlohmann::json{
            {"error", "获取项目结构时发生未知错误"},
            {"success", false}
        };
    }
}

// ==================== 工具注册 ====================

void CodeTools::registerGetProjectStructureTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "get_project_structure";
    tool.description = R"(分析项目结构，包括目录结构、源文件列表、CMAKE配置和依赖关系。
    
重要特性：
- 智能过滤构建目录和临时文件
- 支持超时控制（30秒）和内存限制
- 自动检测项目根目录
- 支持自定义包含/排除模式
- 提供详细的性能统计信息

安全限制：
- 最大递归深度: 8层
- 最大迭代次数: 2000次
- 默认超时: 30秒
- 内存限制: 50MB估算值

使用建议：
- 对于大型项目，建议使用 detail_level="minimal" 或 "normal"
- 可通过 exclude_patterns 排除特定目录以提高性能
- 如果扫描超时，考虑缩小扫描范围或使用更严格的过滤)";
    
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"include_files", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含文件列表（源文件和头文件）"}
            }},
            {"include_dependencies", {
                {"type", "boolean"},
                {"default", true},
                {"description", "是否包含依赖关系（从CMakeLists.txt解析）"}
            }},
            {"project_root", {
                {"type", "string"},
                {"description", "项目根路径，默认自动检测（查找.git、CMakeLists.txt等标识文件）"}
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
                {"description", "详细度级别: minimal（仅目录）、normal（目录+重要文件）、full（所有文件）"}
            }},
            {"max_files", {
                {"type", "integer"},
                {"minimum", 1},
                {"maximum", 2000},
                {"default", 500},
                {"description", "最大文件数量限制（防止输出过大）"}
            }},
            {"max_output_size", {
                {"type", "integer"},
                {"minimum", 1024},
                {"maximum", 5242880},
                {"default", 1048576},
                {"description", "最大输出大小（字节），默认1MB，最大5MB"}
            }},
            {"exclude_patterns", {
                {"type", "array"},
                {"items", {"type", "string"}},
                {"description", "自定义排除模式列表（支持通配符 * 和 ?），例如: [\"test/*\", \"*.tmp\"]"}
            }},
            {"include_patterns", {
                {"type", "array"},
                {"items", {"type", "string"}},
                {"description", "自定义包含模式列表（优先级高于排除模式，支持通配符 * 和 ?）"}
            }}
        }},
        {"required", nlohmann::json::array()}
    };
    
    tool.handler = handleGetProjectStructure;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}

// ==================== 辅助工具：快速扫描 ====================

/**
 * @brief 快速扫描模式（仅获取基本信息，不遍历文件）
 * 
 * 可选：添加一个轻量级的快速扫描函数，用于仅获取项目基本信息
 */
static nlohmann::json handleQuickProjectScan(const nlohmann::json& arguments) {
    try {
        fs::path projectRoot;
        if (arguments.contains("project_root") && arguments["project_root"].is_string()) {
            projectRoot = fs::path(arguments["project_root"].get<std::string>());
        } else {
            projectRoot = detectProjectRoot(fs::current_path());
        }
        
        projectRoot = fs::absolute(projectRoot);
        
        if (!fs::exists(projectRoot) || !fs::is_directory(projectRoot)) {
            return nlohmann::json{{"error", "无效的项目根目录"}};
        }
        
        nlohmann::json result;
        result["root_path"] = pathToUtf8String(projectRoot);
        result["project_name"] = "";
        
        // 解析CMakeLists.txt
        fs::path cmakePath = projectRoot / "CMakeLists.txt";
        nlohmann::json cmakeConfig = parseCMakeLists(cmakePath);
        result["cmake_config"] = cmakeConfig;
        
        if (!cmakeConfig["project_name"].empty()) {
            result["project_name"] = cmakeConfig["project_name"];
        }
        
        // 检测项目类型
        std::vector<std::string> projectTypes;
        if (fs::exists(projectRoot / "CMakeLists.txt")) projectTypes.push_back("cmake");
        if (fs::exists(projectRoot / "package.json")) projectTypes.push_back("nodejs");
        if (fs::exists(projectRoot / "Cargo.toml")) projectTypes.push_back("rust");
        if (fs::exists(projectRoot / "go.mod")) projectTypes.push_back("golang");
        if (fs::exists(projectRoot / "pom.xml")) projectTypes.push_back("maven");
        if (fs::exists(projectRoot / ".git")) projectTypes.push_back("git");
        
        result["project_types"] = projectTypes;
        result["scan_mode"] = "quick";
        
        return result;
        
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("快速扫描失败: ") + e.what()}};
    }
}

/**
 * @brief 注册快速扫描工具（可选）
 */
void CodeTools::registerQuickProjectScanTool(ToolManager& toolManager) {
    ToolDefinition tool;
    tool.name = "quick_project_scan";
    tool.description = "快速扫描项目基本信息（不遍历文件系统），包括项目类型、CMake配置等。适合在完整扫描前快速了解项目概况。";
    
    tool.parametersSchema = nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"project_root", {
                {"type", "string"},
                {"description", "项目根路径，默认自动检测"}
            }}
        }}
    };
    
    tool.handler = handleQuickProjectScan;
    tool.permissionLevel = PermissionLevel::Public;
    
    toolManager.registerTool(tool, true);
}