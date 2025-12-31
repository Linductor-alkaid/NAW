#include "naw/desktop_pet/service/ProjectContextCollector.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service {

// ========== 项目结构分析 ==========

std::string ProjectContextCollector::detectProjectRoot(const std::string& startPath) {
    try {
        fs::path current = fs::absolute(startPath);
        
        // 如果路径是文件，获取其父目录
        if (fs::exists(current) && fs::is_regular_file(current)) {
            current = current.parent_path();
        }
        
        // 向上遍历查找项目标识（限制最大深度，避免无限循环）
        int maxDepth = 20; // 最多向上查找20层
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
                    return current.string();
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
        return fs::absolute(startPath).string();
    } catch (const std::exception&) {
        // 如果出错，返回起始路径
        return startPath;
    }
}

nlohmann::json ProjectContextCollector::parseCMakeLists(const std::string& cmakePath) {
    nlohmann::json result;
    result["project_name"] = "";
    result["targets"] = nlohmann::json::array();
    result["dependencies"] = nlohmann::json::array();
    result["compile_options"] = nlohmann::json::array();
    result["include_directories"] = nlohmann::json::array();
    
    fs::path path(cmakePath);
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        return result;
    }
    
    try {
        std::ifstream file(path, std::ios::in);
        if (!file.is_open()) {
            return result;
        }
        
        std::string line;
        std::regex projectRegex(R"(project\s*\(\s*(\w+))");
        std::regex addExecutableRegex(R"(add_executable\s*\(\s*(\w+))");
        std::regex addLibraryRegex(R"(add_library\s*\(\s*(\w+))");
        std::regex targetLinkLibrariesRegex(R"(target_link_libraries\s*\(\s*(\w+)\s+(.+)\))");
        std::regex findPackageRegex(R"(find_package\s*\(\s*(\w+))");
        std::regex targetCompileOptionsRegex(R"(target_compile_options\s*\(\s*(\w+)\s+(.+)\))");
        std::regex targetCompileDefinitionsRegex(R"(target_compile_definitions\s*\(\s*(\w+)\s+(.+)\))");
        std::regex targetIncludeDirectoriesRegex(R"(target_include_directories\s*\(\s*(\w+)\s+(.+)\))");
        
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
            
            // 解析 target_compile_options()
            if (std::regex_search(line, match, targetCompileOptionsRegex)) {
                std::string options = match[2].str();
                std::istringstream iss(options);
                std::string opt;
                while (iss >> opt) {
                    if (!opt.empty()) {
                        result["compile_options"].push_back(opt);
                    }
                }
            }
            
            // 解析 target_compile_definitions()
            if (std::regex_search(line, match, targetCompileDefinitionsRegex)) {
                std::string defs = match[2].str();
                std::istringstream iss(defs);
                std::string def;
                while (iss >> def) {
                    if (!def.empty()) {
                        result["compile_options"].push_back(def);
                    }
                }
            }
            
            // 解析 target_include_directories()
            if (std::regex_search(line, match, targetIncludeDirectoriesRegex)) {
                std::string dirs = match[2].str();
                std::istringstream iss(dirs);
                std::string dir;
                while (iss >> dir) {
                    if (!dir.empty()) {
                        result["include_directories"].push_back(dir);
                    }
                }
            }
        }
        
        file.close();
    } catch (...) {
        // 解析失败，返回空结果
    }
    
    return result;
}

std::string ProjectContextCollector::identifyFileType(const std::string& filePath) {
    fs::path path(filePath);
    std::string ext = path.extension().string();
    
    // 转换为小写进行比较
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // C++ 源文件
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
        return "cpp";
    }
    // C++ 头文件
    if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
        return "header";
    }
    // Python文件
    if (ext == ".py") {
        return "python";
    }
    // CMake文件
    if (path.filename().string() == "CMakeLists.txt" || ext == ".cmake") {
        return "cmake";
    }
    // 配置文件
    if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml") {
        return "config";
    }
    
    return "other";
}

ProjectInfo ProjectContextCollector::analyzeProject(const std::string& projectRoot, ErrorInfo* error) {
    ProjectInfo info;
    info.rootPath = fs::absolute(projectRoot).string();
    
    try {
        fs::path rootPath(info.rootPath);
        if (!fs::exists(rootPath)) {
            if (error) {
                error->errorType = ErrorType::InvalidRequest;
                error->message = "项目根目录不存在: " + info.rootPath;
            }
            return info;
        }
        
        // 解析 CMakeLists.txt
        fs::path cmakePath = rootPath / "CMakeLists.txt";
        info.cmakeConfig = parseCMakeLists(cmakePath.string());
        if (info.cmakeConfig.contains("project_name") && 
            !info.cmakeConfig["project_name"].empty()) {
            info.name = info.cmakeConfig["project_name"].get<std::string>();
        } else {
            info.name = rootPath.filename().string();
        }
        
        // 提取依赖
        info.dependencies = extractDependenciesFromCMake(info.cmakeConfig);
        
        // 扫描目录结构
        std::vector<std::string> sourceFiles;
        std::vector<std::string> headerFiles;
        
        try {
            fs::recursive_directory_iterator dirIter(rootPath, 
                fs::directory_options::skip_permission_denied);
            for (const auto& entry : dirIter) {
                try {
                    // 跳过符号链接，避免无限循环
                    if (fs::is_symlink(entry)) {
                        dirIter.disable_recursion_pending();
                        continue;
                    }
                    
                    if (fs::is_regular_file(entry)) {
                        std::string filePath = entry.path().string();
                        std::string fileType = identifyFileType(filePath);
                        
                        if (fileType == "cpp") {
                            sourceFiles.push_back(filePath);
                        } else if (fileType == "header") {
                            headerFiles.push_back(filePath);
                        }
                    }
                } catch (const fs::filesystem_error&) {
                    // 跳过权限错误等文件系统错误
                    dirIter.disable_recursion_pending();
                    continue;
                } catch (...) {
                    continue;
                }
            }
        } catch (...) {
            // 忽略遍历错误
        }
        
        info.sourceFiles = std::move(sourceFiles);
        info.headerFiles = std::move(headerFiles);
        
        // 构建目录结构
        info.directoryStructure = buildDirectoryStructure(info.rootPath);
        
    } catch (const std::exception& e) {
        if (error) {
            error->errorType = ErrorType::UnknownError;
            error->message = std::string("分析项目失败: ") + e.what();
        }
    } catch (...) {
        if (error) {
            error->errorType = ErrorType::UnknownError;
            error->message = "分析项目时发生未知错误";
        }
    }
    
    return info;
}

std::string ProjectContextCollector::buildDirectoryStructure(const std::string& projectRoot, int maxDepth) {
    std::ostringstream structure;
    try {
        fs::path rootPath(projectRoot);
        fs::recursive_directory_iterator dirIter(rootPath, 
            fs::directory_options::skip_permission_denied);
        std::vector<fs::path> paths;
        
        // 先收集所有路径（限制数量，避免过大）
        const size_t MAX_PATHS = 1000;
        int currentDepth = 0;
        for (const auto& entry : dirIter) {
            try {
                // 跳过符号链接，避免无限循环
                if (fs::is_symlink(entry)) {
                    dirIter.disable_recursion_pending();
                    continue;
                }
                
                // 计算深度
                fs::path relPath = fs::relative(entry.path(), rootPath);
                int depth = std::distance(relPath.begin(), relPath.end()) - 1;
                if (depth > maxDepth) {
                    dirIter.disable_recursion_pending();
                    continue;
                }
                
                paths.push_back(entry.path());
                if (paths.size() >= MAX_PATHS) {
                    break; // 限制路径数量，避免卡死
                }
            } catch (const fs::filesystem_error&) {
                // 跳过权限错误等文件系统错误
                dirIter.disable_recursion_pending();
                continue;
            } catch (...) {
                continue;
            }
        }
        
        // 构建简化的目录结构
        for (const auto& path : paths) {
            try {
                // 计算相对路径
                fs::path relPath = fs::relative(path, rootPath);
                if (relPath.empty() || relPath == ".") {
                    continue; // 跳过根目录本身
                }
                
                structure << relPath.string();
                if (fs::is_directory(path)) {
                    structure << "/";
                }
                structure << "\n";
            } catch (...) {
                // 如果无法计算相对路径，使用文件名
                structure << path.filename().string();
                if (fs::is_directory(path)) {
                    structure << "/";
                }
                structure << "\n";
            }
        }
    } catch (...) {
        structure << "无法构建目录结构";
    }
    return structure.str();
}

// ========== 依赖关系提取 ==========

std::vector<std::string> ProjectContextCollector::extractDependenciesFromCMake(const nlohmann::json& cmakeConfig) {
    std::vector<std::string> dependencies;
    
    if (!cmakeConfig.contains("dependencies") || !cmakeConfig["dependencies"].is_array()) {
        return dependencies;
    }
    
    for (const auto& dep : cmakeConfig["dependencies"]) {
        if (dep.is_string()) {
            std::string depStr = dep.get<std::string>();
            // 规范化依赖名称（去除版本号、路径等）
            // 简单实现：去除路径分隔符后的部分
            size_t pos = depStr.find_last_of("/\\");
            if (pos != std::string::npos) {
                depStr = depStr.substr(pos + 1);
            }
            // 去除版本号（如 boost-1.70 -> boost）
            pos = depStr.find('-');
            if (pos != std::string::npos && pos > 0) {
                // 检查是否是版本号格式（数字开头）
                if (pos + 1 < depStr.length() && std::isdigit(depStr[pos + 1])) {
                    depStr = depStr.substr(0, pos);
                }
            }
            if (!depStr.empty()) {
                dependencies.push_back(depStr);
            }
        }
    }
    
    // 去重
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    
    return dependencies;
}

std::vector<std::string> ProjectContextCollector::extractIncludesFromSource(
    const std::string& filePath,
    const ProjectInfo& projectInfo
) {
    std::vector<std::string> includes;
    
    try {
        std::ifstream file(filePath, std::ios::in);
        if (!file.is_open()) {
            return includes;
        }
        
        std::string line;
        std::regex includeRegex(R"(#include\s*[<"]([^>"]+)[>"])");
        std::regex importRegex(R"(^import\s+(\w+))");
        
        fs::path filePathObj(filePath);
        std::string fileType = identifyFileType(filePath);
        
        while (std::getline(file, line)) {
            std::smatch match;
            
            if (fileType == "cpp" || fileType == "header") {
                // C++ include
                if (std::regex_search(line, match, includeRegex)) {
                    std::string includePath = match[1].str();
                    // 检查是否是项目内文件（不是系统头文件）
                    if (includePath.find('<') == std::string::npos) {
                        // 尝试在项目中查找该文件
                        fs::path includePathObj(includePath);
                        for (const auto& headerFile : projectInfo.headerFiles) {
                            fs::path headerPath(headerFile);
                            if (headerPath.filename() == includePathObj.filename() ||
                                headerPath.string().find(includePath) != std::string::npos) {
                                includes.push_back(headerFile);
                                break;
                            }
                        }
                    }
                }
            } else if (fileType == "python") {
                // Python import
                if (std::regex_search(line, match, importRegex)) {
                    std::string moduleName = match[1].str();
                    // 尝试在项目中查找该模块
                    for (const auto& sourceFile : projectInfo.sourceFiles) {
                        fs::path sourcePath(sourceFile);
                        if (sourcePath.stem().string() == moduleName) {
                            includes.push_back(sourceFile);
                            break;
                        }
                    }
                }
            }
        }
        
        file.close();
    } catch (...) {
        // 解析失败，返回空列表
    }
    
    return includes;
}

// ========== 文件上下文收集 ==========

std::vector<std::string> ProjectContextCollector::findFilesIncluding(
    const std::string& targetFile,
    const ProjectInfo& projectInfo
) {
    std::vector<std::string> includingFiles;
    
    fs::path targetPath(targetFile);
    std::string targetFileName = targetPath.filename().string();
    
    // 在所有源文件和头文件中查找包含该文件的文件
    std::vector<std::string> allFiles = projectInfo.sourceFiles;
    allFiles.insert(allFiles.end(), projectInfo.headerFiles.begin(), projectInfo.headerFiles.end());
    
    for (const auto& file : allFiles) {
        try {
            std::ifstream f(file, std::ios::in);
            if (!f.is_open()) {
                continue;
            }
            
            std::string line;
            std::regex includeRegex(R"(#include\s*[<"]([^>"]+)[>"])");
            
            while (std::getline(f, line)) {
                std::smatch match;
                if (std::regex_search(line, match, includeRegex)) {
                    std::string includePath = match[1].str();
                    fs::path includePathObj(includePath);
                    if (includePathObj.filename().string() == targetFileName ||
                        includePath == targetFileName) {
                        includingFiles.push_back(file);
                        break;
                    }
                }
            }
            
            f.close();
        } catch (...) {
            continue;
        }
    }
    
    return includingFiles;
}

std::vector<std::string> ProjectContextCollector::findRelatedFiles(
    const std::string& filePath,
    const ProjectInfo& projectInfo
) {
    std::vector<std::string> relatedFiles;
    
    // 查找该文件包含的文件
    std::vector<std::string> includes = extractIncludesFromSource(filePath, projectInfo);
    relatedFiles.insert(relatedFiles.end(), includes.begin(), includes.end());
    
    // 查找包含该文件的文件（反向依赖）
    std::vector<std::string> includingFiles = findFilesIncluding(filePath, projectInfo);
    relatedFiles.insert(relatedFiles.end(), includingFiles.begin(), includingFiles.end());
    
    // 去重
    std::sort(relatedFiles.begin(), relatedFiles.end());
    relatedFiles.erase(std::unique(relatedFiles.begin(), relatedFiles.end()), relatedFiles.end());
    
    return relatedFiles;
}

bool ProjectContextCollector::needsCacheUpdate(const std::string& filePath) {
    try {
        if (!fs::exists(filePath)) {
            // 文件不存在，清除缓存
            std::lock_guard<std::mutex> lock(m_mutex);
            m_fileCache.erase(filePath);
            m_fileModifyTime.erase(filePath);
            return true;
        }
        
        auto currentModifyTime = fs::last_write_time(filePath);
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_fileModifyTime.find(filePath);
            if (it == m_fileModifyTime.end()) {
                return true; // 缓存中没有，需要更新
            }
            
            return currentModifyTime != it->second; // 修改时间不同，需要更新
        }
    } catch (...) {
        return true; // 出错时，假设需要更新
    }
}

std::string ProjectContextCollector::readFileWithCache(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 检查缓存
    if (!needsCacheUpdate(filePath)) {
        auto it = m_fileCache.find(filePath);
        if (it != m_fileCache.end()) {
            return it->second;
        }
    }
    
    // 读取文件
    std::string content;
    try {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        // 读取整个文件
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        content.resize(size);
        file.read(&content[0], size);
        file.close();
        
        // 更新缓存
        m_fileCache[filePath] = content;
        try {
            m_fileModifyTime[filePath] = fs::last_write_time(filePath);
        } catch (...) {
            // 忽略时间获取错误
        }
    } catch (...) {
        return "";
    }
    
    return content;
}

std::string ProjectContextCollector::getFileContext(
    const std::string& filePath,
    const ProjectInfo& projectInfo,
    int maxDepth,
    size_t maxFiles,
    size_t maxTokens
) {
    std::ostringstream context;
    
    // 读取主文件内容
    std::string mainContent = readFileWithCache(filePath);
    if (!mainContent.empty()) {
        context << "=== " << filePath << " ===\n";
        context << mainContent << "\n\n";
    }
    
    // 查找相关文件
    std::vector<std::string> relatedFiles = findRelatedFiles(filePath, projectInfo);
    
    // 限制文件数量
    if (relatedFiles.size() > maxFiles) {
        relatedFiles.resize(maxFiles);
    }
    
    // 读取相关文件内容
    size_t currentTokens = 0; // 简单估算：字符数 / 4
    for (const auto& relatedFile : relatedFiles) {
        if (maxTokens > 0) {
            std::string content = readFileWithCache(relatedFile);
            size_t estimatedTokens = content.length() / 4;
            if (currentTokens + estimatedTokens > maxTokens) {
                break; // 超过Token限制
            }
            currentTokens += estimatedTokens;
        }
        
        std::string content = readFileWithCache(relatedFile);
        if (!content.empty()) {
            context << "=== " << relatedFile << " ===\n";
            context << content << "\n\n";
        }
    }
    
    return context.str();
}

// ========== 项目摘要生成 ==========

std::string ProjectContextCollector::getProjectSummary(const ProjectInfo& projectInfo, size_t maxLength) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 检查摘要缓存
    auto it = m_summaryCache.find(projectInfo.rootPath);
    if (it != m_summaryCache.end()) {
        // 检查是否需要更新（通过检查CMakeLists.txt的修改时间）
        try {
            fs::path cmakePath = fs::path(projectInfo.rootPath) / "CMakeLists.txt";
            if (fs::exists(cmakePath)) {
                auto currentModifyTime = fs::last_write_time(cmakePath);
                auto cacheIt = m_summaryModifyTime.find(projectInfo.rootPath);
                if (cacheIt != m_summaryModifyTime.end() && 
                    currentModifyTime == cacheIt->second) {
                    return it->second; // 缓存有效
                }
            }
        } catch (...) {
            // 如果检查失败，继续生成新摘要
        }
    }
    
    // 生成新摘要
    std::ostringstream summary;
    
    // 项目基本信息
    summary << "# Project: " << projectInfo.name << "\n\n";
    summary << "**Root Path:** " << projectInfo.rootPath << "\n\n";
    
    // 项目结构摘要
    summary << "## Structure\n\n";
    summary << "- Source Files: " << projectInfo.sourceFiles.size() << "\n";
    summary << "- Header Files: " << projectInfo.headerFiles.size() << "\n";
    
    if (projectInfo.directoryStructure.has_value()) {
        std::string structure = *projectInfo.directoryStructure;
        // 限制结构长度
        if (structure.length() > 500) {
            structure = structure.substr(0, 500) + "...\n(truncated)";
        }
        summary << "\n**Directory Structure:**\n```\n" << structure << "```\n";
    }
    
    // 依赖关系
    if (!projectInfo.dependencies.empty()) {
        summary << "\n## Dependencies\n\n";
        for (size_t i = 0; i < projectInfo.dependencies.size() && i < 20; ++i) {
            summary << "- " << projectInfo.dependencies[i] << "\n";
        }
        if (projectInfo.dependencies.size() > 20) {
            summary << "- ... (" << (projectInfo.dependencies.size() - 20) << " more)\n";
        }
    }
    
    // CMake配置要点
    if (!projectInfo.cmakeConfig.empty()) {
        summary << "\n## Build Configuration\n\n";
        if (projectInfo.cmakeConfig.contains("targets") && 
            projectInfo.cmakeConfig["targets"].is_array() && 
            !projectInfo.cmakeConfig["targets"].empty()) {
            summary << "**Targets:**\n";
            for (const auto& target : projectInfo.cmakeConfig["targets"]) {
                if (target.is_string()) {
                    summary << "- " << target.get<std::string>() << "\n";
                }
            }
        }
    }
    
    std::string summaryStr = summary.str();
    
    // 限制长度
    if (summaryStr.length() > maxLength) {
        summaryStr = summaryStr.substr(0, maxLength) + "\n...(truncated)";
    }
    
    // 更新缓存
    m_summaryCache[projectInfo.rootPath] = summaryStr;
    try {
        fs::path cmakePath = fs::path(projectInfo.rootPath) / "CMakeLists.txt";
        if (fs::exists(cmakePath)) {
            m_summaryModifyTime[projectInfo.rootPath] = fs::last_write_time(cmakePath);
        }
    } catch (...) {
        // 忽略时间获取错误
    }
    
    return summaryStr;
}

// ========== 与 ContextManager 集成 ==========

ProjectContext ProjectContextCollector::collectProjectContext(
    const std::string& projectRoot,
    ErrorInfo* error
) {
    ProjectContext context;
    
    // 分析项目
    ProjectInfo info = analyzeProject(projectRoot, error);
    if (error && !error->message.empty()) {
        return context; // 分析失败，返回空上下文
    }
    
    // 设置项目根路径
    context.projectRoot = info.rootPath;
    
    // 生成项目摘要
    context.structureSummary = getProjectSummary(info);
    
    // 收集相关文件（可以选择一些关键文件）
    context.relevantFiles.clear();
    // 添加一些关键文件（如主要的源文件和头文件）
    if (!info.sourceFiles.empty()) {
        // 选择前10个源文件
        size_t count = std::min(size_t(10), info.sourceFiles.size());
        context.relevantFiles.insert(
            context.relevantFiles.end(),
            info.sourceFiles.begin(),
            info.sourceFiles.begin() + count
        );
    }
    if (!info.headerFiles.empty()) {
        // 选择前10个头文件
        size_t count = std::min(size_t(10), info.headerFiles.size());
        context.relevantFiles.insert(
            context.relevantFiles.end(),
            info.headerFiles.begin(),
            info.headerFiles.begin() + count
        );
    }
    
    return context;
}

// ========== 缓存管理 ==========

void ProjectContextCollector::clearFileCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileCache.clear();
    m_fileModifyTime.clear();
}

void ProjectContextCollector::clearSummaryCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_summaryCache.clear();
    m_summaryModifyTime.clear();
}

void ProjectContextCollector::clearAllCaches() {
    clearFileCache();
    clearSummaryCache();
}

} // namespace naw::desktop_pet::service

