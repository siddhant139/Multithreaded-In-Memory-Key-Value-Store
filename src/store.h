#pragma once

#include "types.h"
#include "lru_cache.h"

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace kvstore {

// Thread-safe in-memory key-value store with LRU eviction and TTL support
// Uses std::shared_mutex for concurrent reads, exclusive writes
class KVStore {
public:
    explicit KVStore(size_t max_entries = DEFAULT_MAX_ENTRIES);

    // Core operations
    bool set(const std::string& key, const std::string& value,
             std::optional<int> ttl_seconds = std::nullopt);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);

    // Bulk operations
    size_t del(const std::vector<std::string>& keys);

    // TTL operations
    std::optional<int> ttl(const std::string& key);  // remaining TTL in seconds, -1 if no expiry

    // Info
    size_t size() const;
    size_t maxEntries() const;
    std::vector<std::string> keys() const;

    // Stats
    uint64_t totalGets() const;
    uint64_t totalSets() const;
    uint64_t totalHits() const;
    uint64_t totalMisses() const;
    uint64_t totalEvictions() const;

    // Called by ExpiryManager to purge expired keys
    size_t purgeExpired(size_t sample_size = 20);

private:
    std::unordered_map<std::string, Entry> data_;
    LRUEvictionPolicy lru_;
    mutable std::shared_mutex rw_mutex_;

    // Stats counters
    std::atomic<uint64_t> total_gets_{0};
    std::atomic<uint64_t> total_sets_{0};
    std::atomic<uint64_t> total_hits_{0};
    std::atomic<uint64_t> total_misses_{0};
    std::atomic<uint64_t> total_evictions_{0};

    bool isExpired(const Entry& entry) const;
    void evictIfNeeded();
};

} // namespace kvstore
