#pragma once

#include <string>
#include <list>
#include <unordered_map>
#include <optional>
#include <cstddef>

namespace kvstore {

// LRU eviction policy using doubly-linked list + hashmap
// O(1) for all operations: access, insert, delete, evict
class LRUEvictionPolicy {
public:
    explicit LRUEvictionPolicy(size_t max_entries);

    // Record that a key was accessed (move to front)
    void recordAccess(const std::string& key);

    // Record that a key was inserted (add to front)
    void recordInsert(const std::string& key);

    // Record that a key was deleted (remove from list)
    void recordDelete(const std::string& key);

    // Evict the least recently used key (remove from back)
    // Returns the key that was evicted, or nullopt if empty
    std::optional<std::string> evict();

    // Check if eviction is needed
    bool shouldEvict() const;

    // Current number of tracked entries
    size_t size() const;

    // Maximum entries before eviction
    size_t maxEntries() const;

private:
    size_t max_entries_;
    std::list<std::string> order_;  // front = most recent, back = least recent
    std::unordered_map<std::string, std::list<std::string>::iterator> lookup_;
};

} // namespace kvstore
