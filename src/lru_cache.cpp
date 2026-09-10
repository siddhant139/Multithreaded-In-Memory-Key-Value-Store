#include "lru_cache.h"

namespace kvstore {

LRUEvictionPolicy::LRUEvictionPolicy(size_t max_entries)
    : max_entries_(max_entries) {}

void LRUEvictionPolicy::recordAccess(const std::string& key) {
    auto it = lookup_.find(key);
    if (it == lookup_.end()) return;

    // Move to front of the list (most recently used)
    order_.erase(it->second);
    order_.push_front(key);
    it->second = order_.begin();
}

void LRUEvictionPolicy::recordInsert(const std::string& key) {
    // If key already exists, treat as access
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
        recordAccess(key);
        return;
    }

    order_.push_front(key);
    lookup_[key] = order_.begin();
}

void LRUEvictionPolicy::recordDelete(const std::string& key) {
    auto it = lookup_.find(key);
    if (it == lookup_.end()) return;

    order_.erase(it->second);
    lookup_.erase(it);
}

std::optional<std::string> LRUEvictionPolicy::evict() {
    if (order_.empty()) return std::nullopt;

    std::string evicted_key = order_.back();
    order_.pop_back();
    lookup_.erase(evicted_key);
    return evicted_key;
}

bool LRUEvictionPolicy::shouldEvict() const {
    return order_.size() >= max_entries_;
}

size_t LRUEvictionPolicy::size() const {
    return order_.size();
}

size_t LRUEvictionPolicy::maxEntries() const {
    return max_entries_;
}

} // namespace kvstore
