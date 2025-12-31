#include "naw/desktop_pet/service/tools/ProjectStructureCache.h"
#include "naw/desktop_pet/service/tools/CodeToolsUtils.h"

#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;
using namespace naw::desktop_pet::service::tools;

namespace {
    // 计算字符串哈希
    std::string computeHash(const std::string& str) {
        std::hash<std::string> hasher;
        size_t hash = hasher(str);
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }
}

ProjectStructureCache::ProjectStructureCache(const fs::path& cacheDir) : cacheDir_(cacheDir) {
    try {
        // 创建缓存目录
        if (!fs::exists(cacheDir_)) {
            fs::create_directories(cacheDir_);
        }
        
        // 清理过期缓存
        evictExpired();
    } catch (...) {
        // 忽略错误
    }
}

std::string ProjectStructureCache::generateKey(
    const fs::path& projectRoot,
    const std::string& detailLevel,
    const std::string& configHash) {
    
    std::string projectRootStr = pathToUtf8String(fs::absolute(projectRoot));
    std::replace(projectRootStr.begin(), projectRootStr.end(), '\\', '/');
    
    std::ostringstream oss;
    oss << projectRootStr << "|" << detailLevel << "|" << configHash;
    return computeHash(oss.str());
}

std::optional<CacheEntry> ProjectStructureCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 先检查内存缓存
    auto it = memoryCache_.find(key);
    if (it != memoryCache_.end()) {
        if (!it->second.isExpired()) {
            stats_.hitCount++;
            // 使用移动语义返回
            return std::make_optional<CacheEntry>(std::move(it->second));
        } else {
            // 已过期，从内存中移除
            memoryCache_.erase(it);
        }
    }
    
    // 从文件系统加载
    auto entryOpt = loadFromFile(key);
    if (entryOpt.has_value() && !entryOpt->isExpired()) {
        // 加载到内存缓存（使用移动语义）
        CacheEntry entry = std::move(entryOpt.value());
        memoryCache_[key] = std::move(entry);
        stats_.hitCount++;
        // 从内存缓存返回（使用移动语义）
        auto it = memoryCache_.find(key);
        if (it != memoryCache_.end()) {
            return std::make_optional<CacheEntry>(std::move(it->second));
        }
    }
    
    stats_.missCount++;
    return std::nullopt;
}

void ProjectStructureCache::put(
    const std::string& key,
    const nlohmann::json& data,
    ProjectFileWhitelist&& whitelist,
    const std::unordered_map<std::string, FileSnapshot>& snapshots,
    std::optional<std::chrono::seconds> ttl) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    CacheEntry entry;
    entry.data = data;
    entry.whitelist = std::move(whitelist);
    entry.snapshots = snapshots;
    entry.timestamp = std::chrono::system_clock::now();
    if (ttl.has_value()) {
        entry.ttl = ttl.value();
    }
    
    // 保存到文件系统（在移动之前）
    saveToFile(key, entry);
    
    // 保存到内存缓存（使用移动语义）
    memoryCache_[key] = std::move(entry);
    
    stats_.totalEntries = memoryCache_.size();
}

std::optional<CacheEntry> ProjectStructureCache::checkAndUpdate(
    const std::string& key,
    const fs::path& projectRoot,
    const ProjectFileWhitelist& whitelist) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 获取现有缓存
    auto cached = get(key);
    if (!cached.has_value()) {
        return std::nullopt;
    }
    
    // 检查配置文件哈希是否变化（在移动之前）
    std::string cachedHash = cached->whitelist.combinedHash;
    if (cachedHash != whitelist.combinedHash) {
        // 配置文件变化，缓存失效
        invalidate(key);
        return std::nullopt;
    }
    
    // 检查文件快照，找出变化的文件
    std::vector<std::string> changedFiles;
    
    for (const auto& [filePath, oldSnapshot] : cached->snapshots) {
        fs::path fullPath = projectRoot / filePath;
        FileSnapshot newSnapshot(fullPath);
        
        if (newSnapshot != oldSnapshot) {
            changedFiles.push_back(filePath);
        }
    }
    
    // 如果有文件变化，需要更新缓存
    // 这里简化处理：如果有变化，返回nullopt让调用者重新扫描
    // 实际实现中可以只更新变化的部分
    if (!changedFiles.empty()) {
        invalidate(key);
        return std::nullopt;
    }
    
    // 没有变化，返回缓存（使用移动语义）
    return std::move(cached);
}

void ProjectStructureCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (key.empty()) {
        clear();
        return;
    }
    
    // 从内存缓存移除
    memoryCache_.erase(key);
    
    // 从文件系统删除
    try {
        fs::path cacheFile = getCacheFilePath(key);
        if (fs::exists(cacheFile)) {
            fs::remove(cacheFile);
        }
    } catch (...) {
        // 忽略错误
    }
}

void ProjectStructureCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    memoryCache_.clear();
    
    // 清空文件系统缓存
    try {
        if (fs::exists(cacheDir_)) {
            for (const auto& entry : fs::directory_iterator(cacheDir_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    fs::remove(entry.path());
                }
            }
        }
    } catch (...) {
        // 忽略错误
    }
    
    stats_.totalEntries = 0;
}

ProjectStructureCache::Statistics ProjectStructureCache::getStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::optional<CacheEntry> ProjectStructureCache::loadFromFile(const std::string& key) {
    try {
        fs::path cacheFile = getCacheFilePath(key);
        if (!fs::exists(cacheFile)) {
            return std::nullopt;
        }
        
        std::ifstream file(cacheFile);
        if (!file.is_open()) {
            return std::nullopt;
        }
        
        nlohmann::json json;
        file >> json;
        
        CacheEntry entry;
        entry.data = json["data"];
        entry.timestamp = std::chrono::system_clock::from_time_t(json["timestamp"]);
        entry.ttl = std::chrono::seconds(json["ttl"]);
        
        // 加载快照（简化处理，实际可以完整序列化）
        if (json.contains("snapshots")) {
            for (const auto& [filePath, snapshotJson] : json["snapshots"].items()) {
                FileSnapshot snapshot;
                snapshot.mtime = std::chrono::system_clock::from_time_t(snapshotJson["mtime"]);
                snapshot.size = snapshotJson["size"];
                entry.snapshots[filePath] = snapshot;
            }
        }
        
        // whitelist 不序列化（包含unique_ptr），从文件加载时为空
        // 使用移动语义返回
        return std::make_optional<CacheEntry>(std::move(entry));
    } catch (...) {
        return std::nullopt;
    }
}

void ProjectStructureCache::saveToFile(const std::string& key, const CacheEntry& entry) {
    try {
        fs::path cacheFile = getCacheFilePath(key);
        
        // 确保目录存在
        fs::create_directories(cacheFile.parent_path());
        
        nlohmann::json json;
        json["data"] = entry.data;
        json["timestamp"] = std::chrono::system_clock::to_time_t(entry.timestamp);
        json["ttl"] = entry.ttl.count();
        
        // 保存快照
        nlohmann::json snapshotsJson;
        for (const auto& [filePath, snapshot] : entry.snapshots) {
            nlohmann::json snapshotJson;
            snapshotJson["mtime"] = std::chrono::system_clock::to_time_t(snapshot.mtime);
            snapshotJson["size"] = snapshot.size;
            snapshotsJson[filePath] = snapshotJson;
        }
        json["snapshots"] = snapshotsJson;
        
        std::ofstream file(cacheFile);
        if (file.is_open()) {
            file << json.dump(2);
        }
    } catch (...) {
        // 忽略错误
    }
}

fs::path ProjectStructureCache::getCacheFilePath(const std::string& key) const {
    // 使用key的前16个字符作为文件名（避免文件名过长）
    std::string filename = key.substr(0, std::min<size_t>(16, key.length())) + ".json";
    return cacheDir_ / filename;
}

void ProjectStructureCache::evictExpired() {
    try {
        if (!fs::exists(cacheDir_)) {
            return;
        }
        
        for (const auto& entry : fs::directory_iterator(cacheDir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    std::ifstream file(entry.path());
                    if (file.is_open()) {
                        nlohmann::json json;
                        file >> json;
                        
                        auto timestamp = std::chrono::system_clock::from_time_t(json["timestamp"]);
                        auto ttl = std::chrono::seconds(json["ttl"]);
                        auto now = std::chrono::system_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
                        
                        if (elapsed > ttl) {
                            fs::remove(entry.path());
                        }
                    }
                } catch (...) {
                    // 忽略错误，继续处理下一个文件
                }
            }
        }
    } catch (...) {
        // 忽略错误
    }
}

