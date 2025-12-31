#include "naw/desktop_pet/service/tools/ProjectWhitelist.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    // 计算字符串的简单哈希
    std::string computeStringHash(const std::string& str) {
        std::hash<std::string> hasher;
        size_t hash = hasher(str);
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }
    
    // 组合多个哈希值
    std::string combineHashes(const std::string& hash1, const std::string& hash2) {
        return computeStringHash(hash1 + "|" + hash2);
    }
}

namespace naw::desktop_pet::service::tools {

bool ProjectFileWhitelist::isWhitelisted(const fs::path& filePath, const fs::path& projectRoot) const {
    try {
        fs::path relPath = fs::relative(filePath, projectRoot);
        if (relPath.empty() || relPath == ".") {
            return false;
        }
        
        std::string relPathStr = pathToUtf8String(relPath);
        std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
        
        // 检查是否在源文件列表中
        if (sourceFiles.find(relPathStr) != sourceFiles.end()) {
            return true;
        }
        
        // 检查是否在配置文件中
        if (configFiles.find(relPathStr) != configFiles.end()) {
            return true;
        }
        
        // 检查是否在包含目录下
        for (const auto& includeDir : includeDirs) {
            if (relPathStr.find(includeDir) == 0) {
                // 检查文件扩展名
                std::string ext = pathToUtf8String(filePath.extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (isHeaderFileExtension(ext) || isSourceFileExtension(ext)) {
                    return true;
                }
            }
        }
        
        // 检查文件扩展名（源文件、头文件、文档文件、资源文件）
        std::string ext = pathToUtf8String(filePath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (isSourceFileExtension(ext) || isHeaderFileExtension(ext) || 
            isDocumentFileExtension(ext) || isResourceFileExtension(ext)) {
            // 检查是否在任何扫描根目录下
            for (const auto& scanRoot : scanRoots) {
                try {
                    fs::path absPath = fs::absolute(filePath);
                    fs::path absScanRoot = fs::absolute(scanRoot);
                    // 使用路径比较而不是字符串比较，避免路径分隔符问题
                    try {
                        fs::path relToRoot = fs::relative(absPath, absScanRoot);
                        // 如果文件在扫描根目录下，relative 不会包含 ".."
                        if (!relToRoot.empty() && relToRoot.string().find("..") != 0) {
                            return true;
                        }
                    } catch (...) {
                        // 如果 relative 失败，回退到字符串比较
                        std::string absPathStr = absPath.string();
                        std::string scanRootStr = absScanRoot.string();
                        // 规范化路径分隔符
                        std::replace(absPathStr.begin(), absPathStr.end(), '\\', '/');
                        std::replace(scanRootStr.begin(), scanRootStr.end(), '\\', '/');
                        if (absPathStr.find(scanRootStr) == 0) {
                            return true;
                        }
                    }
                } catch (...) {
                    // 忽略错误
                }
            }
        }
        
        // 检查是否在文档文件集合中
        if (docFiles.find(relPathStr) != docFiles.end()) {
            return true;
        }
        
        // 检查是否在资源目录下
        for (const auto& resourceDir : resourceDirs) {
            if (relPathStr.find(resourceDir) == 0) {
                return true;
            }
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

bool ProjectFileWhitelist::shouldScanDirectory(const fs::path& dirPath, const fs::path& projectRoot) const {
    try {
        // 检查是否被gitignore忽略
        if (gitIgnoreParser && gitIgnoreParser->isIgnored(dirPath, projectRoot)) {
            return false;
        }
        
        // 检查是否在任何扫描根目录下
        fs::path absDirPath = fs::absolute(dirPath);
        for (const auto& scanRoot : scanRoots) {
            try {
                if (absDirPath.string().find(scanRoot.string()) == 0) {
                    return true;
                }
            } catch (...) {
                // 忽略错误
            }
        }
        
        // 检查是否在包含目录下
        fs::path relPath = fs::relative(dirPath, projectRoot);
        if (!relPath.empty() && relPath != ".") {
            std::string relPathStr = pathToUtf8String(relPath);
            std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
            
            for (const auto& includeDir : includeDirs) {
                if (relPathStr.find(includeDir) == 0 || includeDir.find(relPathStr) == 0) {
                    return true;
                }
            }
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

bool ProjectFileWhitelist::isSourceFileExtension(const std::string& ext) {
    static const std::vector<std::string> sourceExts = {
        ".cpp", ".cc", ".cxx", ".c", ".c++",
        ".java", ".py", ".js", ".ts", ".go", ".rs"
    };
    
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), 
                      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    return std::find(sourceExts.begin(), sourceExts.end(), lowerExt) != sourceExts.end();
}

bool ProjectFileWhitelist::isHeaderFileExtension(const std::string& ext) {
    static const std::vector<std::string> headerExts = {
        ".h", ".hpp", ".hxx", ".h++", ".hh"
    };
    
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), 
                      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    return std::find(headerExts.begin(), headerExts.end(), lowerExt) != headerExts.end();
}

bool ProjectFileWhitelist::isDocumentFileExtension(const std::string& ext) {
    static const std::vector<std::string> docExts = {
        ".md", ".txt", ".rst", ".adoc", ".org",
        ".pdf", ".doc", ".docx", ".html", ".htm"
    };
    
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), 
                      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    return std::find(docExts.begin(), docExts.end(), lowerExt) != docExts.end();
}

bool ProjectFileWhitelist::isResourceFileExtension(const std::string& ext) {
    static const std::vector<std::string> resourceExts = {
        ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico", ".bmp",
        ".json", ".xml", ".yaml", ".yml", ".toml", ".ini", ".conf",
        ".sh", ".bat", ".ps1", ".py", ".js", ".css"
    };
    
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), 
                      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    return std::find(resourceExts.begin(), resourceExts.end(), lowerExt) != resourceExts.end();
}

ProjectFileWhitelist buildProjectWhitelist(
    const fs::path& projectRoot,
    bool useCMakeSources,
    bool useGitIgnore,
    const std::vector<std::string>& scanSrcDirs,
    const std::vector<std::string>& /* excludeDirs */) {  // 未使用参数，用注释标记
    
    ProjectFileWhitelist whitelist;
    
    try {
        fs::path absProjectRoot = fs::absolute(projectRoot);
        
        // 1. 解析CMakeLists.txt
        if (useCMakeSources) {
            whitelist.cmakeInfo = CMakeParser::parseCMakeLists(
                absProjectRoot / "CMakeLists.txt", absProjectRoot);
            whitelist.cmakeHash = whitelist.cmakeInfo.configHash;
            
            // 添加CMake中定义的源文件
            for (const auto& srcFile : whitelist.cmakeInfo.sourceFiles) {
                fs::path srcPath = absProjectRoot / srcFile;
                if (fs::exists(srcPath)) {
                    fs::path relPath = fs::relative(srcPath, absProjectRoot);
                    std::string relPathStr = pathToUtf8String(relPath);
                    std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                    whitelist.sourceFiles.insert(relPathStr);
                }
            }
            
            // 添加包含目录
            for (const auto& includeDir : whitelist.cmakeInfo.includeDirs) {
                fs::path includePath = absProjectRoot / includeDir;
                if (fs::exists(includePath)) {
                    fs::path relPath = fs::relative(includePath, absProjectRoot);
                    std::string relPathStr = pathToUtf8String(relPath);
                    std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                    whitelist.includeDirs.insert(relPathStr);
                }
            }
            
            // 添加子目录到扫描根目录
            for (const auto& subdir : whitelist.cmakeInfo.subdirectories) {
                fs::path subdirPath = absProjectRoot / subdir;
                if (fs::exists(subdirPath) && fs::is_directory(subdirPath)) {
                    whitelist.scanRoots.push_back(fs::absolute(subdirPath));
                }
            }
        }
        
        // 2. 解析.gitignore
        if (useGitIgnore) {
            whitelist.gitIgnoreParser = std::make_unique<GitIgnoreParser>(absProjectRoot);
            whitelist.gitIgnoreParser->parseAll();
            whitelist.gitignoreHash = whitelist.gitIgnoreParser->computeHash();
        }
        
        // 3. 添加默认扫描目录（如果不在CMake中）
        std::vector<std::string> docDirs = {"docs", "doc", "documentation"};
        std::vector<std::string> resourceDirs = {"resources", "assets", "res", "data"};
        
        for (const auto& dirName : scanSrcDirs) {
            fs::path dirPath = absProjectRoot / dirName;
            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                fs::path absDirPath = fs::absolute(dirPath);
                // 检查是否已经在scanRoots中
                bool found = false;
                for (const auto& existing : whitelist.scanRoots) {
                    if (existing == absDirPath) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    whitelist.scanRoots.push_back(absDirPath);
                }
                
                // 标记文档和资源目录
                fs::path relPath = fs::relative(absDirPath, absProjectRoot);
                std::string relPathStr = pathToUtf8String(relPath);
                std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                
                if (std::find(docDirs.begin(), docDirs.end(), dirName) != docDirs.end()) {
                    whitelist.resourceDirs.insert(relPathStr);  // 文档目录也作为资源目录处理
                } else if (std::find(resourceDirs.begin(), resourceDirs.end(), dirName) != resourceDirs.end()) {
                    whitelist.resourceDirs.insert(relPathStr);
                }
            }
        }
        
        // 4. 添加配置文件和文档文件（项目根目录）
        std::vector<std::string> configFileNames = {
            "CMakeLists.txt", ".gitignore", "package.json", "Cargo.toml",
            "go.mod", "pom.xml", "build.gradle", "pyproject.toml"
        };
        
        std::vector<std::string> docFileNames = {
            "README.md", "README.txt", "README.rst", "LICENSE", "LICENSE.txt",
            "CHANGELOG.md", "CHANGELOG.txt", "CONTRIBUTING.md", "AUTHORS", "NOTES"
        };
        
        for (const auto& configName : configFileNames) {
            fs::path configPath = absProjectRoot / configName;
            if (fs::exists(configPath)) {
                fs::path relPath = fs::relative(configPath, absProjectRoot);
                std::string relPathStr = pathToUtf8String(relPath);
                std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                whitelist.configFiles.insert(relPathStr);
            }
        }
        
        // 添加项目根目录的文档文件
        for (const auto& docName : docFileNames) {
            fs::path docPath = absProjectRoot / docName;
            if (fs::exists(docPath) && fs::is_regular_file(docPath)) {
                fs::path relPath = fs::relative(docPath, absProjectRoot);
                std::string relPathStr = pathToUtf8String(relPath);
                std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                whitelist.docFiles.insert(relPathStr);
            }
        }
        
        // 扫描文档目录中的文档文件
        for (const auto& docDirName : docDirs) {
            fs::path docDirPath = absProjectRoot / docDirName;
            if (fs::exists(docDirPath) && fs::is_directory(docDirPath)) {
                try {
                    for (const auto& entry : fs::recursive_directory_iterator(docDirPath,
                            fs::directory_options::skip_permission_denied)) {
                        if (entry.is_regular_file()) {
                            std::string ext = pathToUtf8String(entry.path().extension());
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if (ProjectFileWhitelist::isDocumentFileExtension(ext)) {
                                fs::path relPath = fs::relative(entry.path(), absProjectRoot);
                                std::string relPathStr = pathToUtf8String(relPath);
                                std::replace(relPathStr.begin(), relPathStr.end(), '\\', '/');
                                whitelist.docFiles.insert(relPathStr);
                            }
                        }
                    }
                } catch (...) {
                    // 忽略错误
                }
            }
        }
        
        // 5. 如果没有扫描根目录，添加项目根目录
        if (whitelist.scanRoots.empty()) {
            whitelist.scanRoots.push_back(absProjectRoot);
        }
        
        // 6. 计算组合哈希
        whitelist.combinedHash = combineHashes(whitelist.cmakeHash, whitelist.gitignoreHash);
        
    } catch (...) {
        // 构建失败，返回空白名单
    }
    
    return whitelist;
}

} // namespace naw::desktop_pet::service::tools
