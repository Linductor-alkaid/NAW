#pragma once

#include "naw/desktop_pet/service/tools/ProjectWhitelist.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace naw::desktop_pet::service::tools {

/**
 * @brief 文件快照（用于增量更新）
 */
struct FileSnapshot {
    std::chrono::system_clock::time_point mtime;  // 修改时间
    uint64_t size;                                  // 文件大小
    
    FileSnapshot() : size(0) {
        mtime = std::chrono::system_clock::time_point::min();
    }
    
    FileSnapshot(const fs::path& filePath) {
        try {
            if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
                auto ftime = fs::last_write_time(filePath);
                mtime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                size = fs::file_size(filePath);
            } else {
                mtime = std::chrono::system_clock::time_point::min();
                size = 0;
            }
        } catch (...) {
            mtime = std::chrono::system_clock::time_point::min();
            size = 0;
        }
    }
    
    bool operator==(const FileSnapshot& other) const {
        return mtime == other.mtime && size == other.size;
    }
    
    bool operator!=(const FileSnapshot& other) const {
        return !(*this == other);
    }
};

/**
 * @brief 缓存条目
 */
struct CacheEntry {
    nlohmann::json data;                                    // 缓存的数据
    std::chrono::system_clock::time_point timestamp;        // 缓存时间
    std::chrono::seconds ttl;                               // 生存时间
    std::unordered_map<std::string, FileSnapshot> snapshots; // 文件快照
    ProjectFileWhitelist whitelist;                         // 白名单（用于增量更新）
    
    CacheEntry() : ttl(std::chrono::hours(1)) {
        timestamp = std::chrono::system_clock::now();
    }
    
    // 移动构造函数
    CacheEntry(CacheEntry&&) noexcept = default;
    
    // 移动赋值运算符
    CacheEntry& operator=(CacheEntry&&) noexcept = default;
    
    // 禁用复制（因为包含ProjectFileWhitelist，而它包含unique_ptr）
    CacheEntry(const CacheEntry&) = delete;
    CacheEntry& operator=(const CacheEntry&) = delete;
    
    bool isExpired() const {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
        return elapsed > ttl;
    }
};

/**
 * @brief 项目结构缓存管理器
 * 
 * 实现持久化缓存和增量更新：
 * - 基于项目根路径和配置文件哈希生成缓存键
 * - 文件系统持久化缓存（JSON格式）
 * - 增量更新：比较文件修改时间，只扫描变化的部分
 * - 分层缓存：为不同detail_level分别缓存
 */
class ProjectStructureCache {
public:
    /**
     * @brief 构造函数
     * @param cacheDir 缓存目录（默认：系统临时目录）
     */
    explicit ProjectStructureCache(const fs::path& cacheDir = fs::temp_directory_path() / "naw_project_cache");
    
    /**
     * @brief 生成缓存键
     * @param projectRoot 项目根目录
     * @param detailLevel 详细度级别
     * @param configHash 配置文件哈希（CMake + GitIgnore）
     * @return 缓存键（字符串）
     */
    static std::string generateKey(
        const fs::path& projectRoot,
        const std::string& detailLevel,
        const std::string& configHash);
    
    /**
     * @brief 获取缓存
     * @param key 缓存键
     * @return 缓存条目，如果未命中或已过期则返回nullopt
     */
    std::optional<CacheEntry> get(const std::string& key);
    
    /**
     * @brief 存储缓存
     * @param key 缓存键
     * @param data 缓存数据
     * @param whitelist 白名单（将被移动）
     * @param snapshots 文件快照
     * @param ttl 生存时间（可选，默认1小时）
     */
    void put(
        const std::string& key,
        const nlohmann::json& data,
        ProjectFileWhitelist&& whitelist,
        const std::unordered_map<std::string, FileSnapshot>& snapshots,
        std::optional<std::chrono::seconds> ttl = std::nullopt);
    
    /**
     * @brief 检查并更新缓存（增量更新）
     * @param key 缓存键
     * @param projectRoot 项目根目录
     * @param whitelist 当前白名单（用于比较哈希）
     * @return 更新后的缓存条目，如果无法更新则返回nullopt
     */
    std::optional<CacheEntry> checkAndUpdate(
        const std::string& key,
        const fs::path& projectRoot,
        const ProjectFileWhitelist& whitelist);
    
    /**
     * @brief 使缓存失效
     * @param key 缓存键（如果为空则清除所有缓存）
     */
    void invalidate(const std::string& key = "");
    
    /**
     * @brief 清空所有缓存
     */
    void clear();
    
    /**
     * @brief 获取缓存统计信息
     */
    struct Statistics {
        size_t totalEntries;
        size_t totalSize;
        size_t hitCount;
        size_t missCount;
        
        double getHitRate() const {
            size_t total = hitCount + missCount;
            return total > 0 ? static_cast<double>(hitCount) / total : 0.0;
        }
    };
    
    Statistics getStatistics() const;

private:
    /**
     * @brief 从文件系统加载缓存
     */
    std::optional<CacheEntry> loadFromFile(const std::string& key);
    
    /**
     * @brief 保存缓存到文件系统
     */
    void saveToFile(const std::string& key, const CacheEntry& entry);
    
    /**
     * @brief 获取缓存文件路径
     */
    fs::path getCacheFilePath(const std::string& key) const;
    
    /**
     * @brief 清理过期缓存
     */
    void evictExpired();

private:
    fs::path cacheDir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> memoryCache_;  // 内存缓存
    Statistics stats_;
};

} // namespace naw::desktop_pet::service::tools

