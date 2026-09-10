#include "store.h"

#include <algorithm>
#include <random>

namespace kvstore {

KVStore::KVStore(size_t max_entries) : lru_(max_entries) {}

bool KVStore::set(const std::string& key, const std::string& value,
                  std::optional<int> ttl_seconds) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    total_sets_.fetch_add(1, std::memory_order_relaxed);

    Entry entry;
    entry.value = value;
    entry.created_at = Clock::now();

    if (ttl_seconds.has_value() && ttl_seconds.value() > 0) {
        entry.expires_at = Clock::now() + std::chrono::seconds(ttl_seconds.value());
    }

    // Check if key already exists (update, no eviction needed)
    auto it = data_.find(key);
    if (it != data_.end()) {
        it->second = std::move(entry);
        lru_.recordAccess(key);
        return true;
    }

    // New key — may need eviction
    evictIfNeeded();

    data_.emplace(key, std::move(entry));
    lru_.recordInsert(key);
    return true;
}

std::optional<std::string> KVStore::get(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    total_gets_.fetch_add(1, std::memory_order_relaxed);

    auto it = data_.find(key);
    if (it == data_.end()) {
        total_misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    // Lazy expiry check
    if (isExpired(it->second)) {
        lru_.recordDelete(key);
        data_.erase(it);
        total_misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    total_hits_.fetch_add(1, std::memory_order_relaxed);
    lru_.recordAccess(key);
    return it->second.value;
}

bool KVStore::del(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) return false;

    lru_.recordDelete(key);
    data_.erase(it);
    return true;
}

size_t KVStore::del(const std::vector<std::string>& keys) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    size_t deleted = 0;

    for (const auto& key : keys) {
        auto it = data_.find(key);
        if (it != data_.end()) {
            lru_.recordDelete(key);
            data_.erase(it);
            ++deleted;
        }
    }

    return deleted;
}

bool KVStore::exists(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) return false;

    // Check expiry
    if (isExpired(it->second)) return false;

    return true;
}

std::optional<int> KVStore::ttl(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;

    if (!it->second.expires_at.has_value()) {
        return -1;  // No expiry set
    }

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second.expires_at.value() - Clock::now());

    if (remaining.count() <= 0) return 0;  // Already expired
    return static_cast<int>(remaining.count());
}

size_t KVStore::size() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return data_.size();
}

size_t KVStore::maxEntries() const {
    return lru_.maxEntries();
}

std::vector<std::string> KVStore::keys() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    std::vector<std::string> result;
    result.reserve(data_.size());
    for (const auto& [key, _] : data_) {
        result.push_back(key);
    }
    return result;
}

uint64_t KVStore::totalGets() const { return total_gets_.load(std::memory_order_relaxed); }
uint64_t KVStore::totalSets() const { return total_sets_.load(std::memory_order_relaxed); }
uint64_t KVStore::totalHits() const { return total_hits_.load(std::memory_order_relaxed); }
uint64_t KVStore::totalMisses() const { return total_misses_.load(std::memory_order_relaxed); }
uint64_t KVStore::totalEvictions() const { return total_evictions_.load(std::memory_order_relaxed); }

size_t KVStore::purgeExpired(size_t sample_size) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    size_t purged = 0;

    if (data_.empty()) return 0;

    // Redis-style sampling: pick random keys and check expiry
    // Repeat if >25% of sampled keys were expired
    std::vector<std::string> all_keys;
    all_keys.reserve(data_.size());
    for (const auto& [key, _] : data_) {
        all_keys.push_back(key);
    }

    static thread_local std::mt19937 rng(std::random_device{}());

    bool keep_going = true;
    while (keep_going && !all_keys.empty()) {
        size_t expired_count = 0;
        size_t checked = 0;
        size_t to_check = std::min(sample_size, all_keys.size());

        // Shuffle and check first 'to_check' keys
        std::shuffle(all_keys.begin(), all_keys.end(), rng);

        for (size_t i = 0; i < to_check; ++i) {
            const auto& key = all_keys[i];
            auto it = data_.find(key);
            if (it != data_.end() && isExpired(it->second)) {
                lru_.recordDelete(key);
                data_.erase(it);
                ++expired_count;
                ++purged;
            }
            ++checked;
        }

        // Redis strategy: continue if >25% were expired
        keep_going = (checked > 0) && (expired_count * 4 > checked);

        // Rebuild keys list after deletions
        if (keep_going) {
            all_keys.clear();
            for (const auto& [key, _] : data_) {
                all_keys.push_back(key);
            }
        }
    }

    return purged;
}

bool KVStore::isExpired(const Entry& entry) const {
    if (!entry.expires_at.has_value()) return false;
    return Clock::now() >= entry.expires_at.value();
}

void KVStore::evictIfNeeded() {
    while (lru_.shouldEvict()) {
        auto evicted = lru_.evict();
        if (evicted.has_value()) {
            data_.erase(evicted.value());
            total_evictions_.fetch_add(1, std::memory_order_relaxed);
        } else {
            break;
        }
    }
}

} // namespace kvstore
