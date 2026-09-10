#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <algorithm>

#include "../src/store.h"

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

void test_basic_set_and_get() {
    std::cout << "\n[Test: Basic set and get]" << std::endl;
    kvstore::KVStore store;
    store.set("key", "value");
    auto result = store.get("key");
    check(result.has_value() && result.value() == "value", "set('key','value'), get('key') returns 'value'");
}

void test_get_nonexistent() {
    std::cout << "\n[Test: Get non-existent key]" << std::endl;
    kvstore::KVStore store;
    auto result = store.get("nonexistent");
    check(!result.has_value(), "get non-existent key returns nullopt");
}

void test_delete() {
    std::cout << "\n[Test: Delete]" << std::endl;
    kvstore::KVStore store;
    store.set("key", "value");
    bool del_existing = store.del("key");
    check(del_existing == true, "delete existing key returns true");

    bool del_nonexistent = store.del("no_such_key");
    check(del_nonexistent == false, "delete non-existent key returns false");
}

void test_get_after_delete() {
    std::cout << "\n[Test: Get after delete]" << std::endl;
    kvstore::KVStore store;
    store.set("key", "value");
    store.del("key");
    auto result = store.get("key");
    check(!result.has_value(), "after delete, get returns nullopt");
}

void test_exists() {
    std::cout << "\n[Test: Exists]" << std::endl;
    kvstore::KVStore store;
    store.set("key", "value");
    check(store.exists("key") == true, "exists returns true for existing key");
    check(store.exists("nonexistent") == false, "exists returns false for non-existent key");
}

void test_set_overwrites() {
    std::cout << "\n[Test: Set overwrites existing value]" << std::endl;
    kvstore::KVStore store;
    store.set("key", "old_value");
    store.set("key", "new_value");
    auto result = store.get("key");
    check(result.has_value() && result.value() == "new_value", "set overwrites existing value");
}

void test_ttl_expiry() {
    std::cout << "\n[Test: TTL lazy expiry]" << std::endl;
    kvstore::KVStore store;
    store.set("ttl_key", "ttl_value", 2);  // EX 2 seconds

    auto result_immediate = store.get("ttl_key");
    check(result_immediate.has_value() && result_immediate.value() == "ttl_value",
          "get immediately after set with TTL returns value");

    std::this_thread::sleep_for(std::chrono::seconds(3));

    auto result_after_expiry = store.get("ttl_key");
    check(!result_after_expiry.has_value(), "get after TTL expires returns nullopt (lazy expiry)");
}

void test_ttl_command_with_expiry() {
    std::cout << "\n[Test: TTL command with expiry]" << std::endl;
    kvstore::KVStore store;
    store.set("ttl_key", "value", 10);  // EX 10 seconds

    auto remaining = store.ttl("ttl_key");
    check(remaining.has_value() && remaining.value() >= 8 && remaining.value() <= 10,
          "ttl() returns value between 8-10 for key with EX 10");
}

void test_ttl_command_no_expiry() {
    std::cout << "\n[Test: TTL command without expiry]" << std::endl;
    kvstore::KVStore store;
    store.set("persistent_key", "value");

    auto remaining = store.ttl("persistent_key");
    check(remaining.has_value() && remaining.value() == -1,
          "ttl() returns -1 for key without expiry");
}

void test_ttl_command_nonexistent() {
    std::cout << "\n[Test: TTL command non-existent key]" << std::endl;
    kvstore::KVStore store;
    auto remaining = store.ttl("no_such_key");
    check(!remaining.has_value(), "ttl() returns nullopt for non-existent key");
}

void test_lru_eviction() {
    std::cout << "\n[Test: LRU eviction]" << std::endl;
    kvstore::KVStore store(3);  // max_entries = 3
    store.set("k1", "v1");
    store.set("k2", "v2");
    store.set("k3", "v3");
    store.set("k4", "v4");  // should trigger eviction of k1

    check(store.size() == 3, "size should be 3 after inserting 4 keys with max=3");
    check(!store.get("k1").has_value(), "first key (k1) should be evicted");
}

void test_lru_access_pattern() {
    std::cout << "\n[Test: LRU access pattern]" << std::endl;
    kvstore::KVStore store(3);  // max_entries = 3
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");

    // Access 'a' to move it to front (most recently used)
    store.get("a");

    // Insert 'd' — should evict 'b' (least recently used), not 'a'
    store.set("d", "4");

    check(store.get("a").has_value(), "a should still exist (was accessed recently)");
    check(!store.get("b").has_value(), "b should be evicted (least recently used)");
    check(store.get("c").has_value(), "c should still exist");
    check(store.get("d").has_value(), "d should exist (just inserted)");
}

void test_bulk_delete() {
    std::cout << "\n[Test: Bulk delete]" << std::endl;
    kvstore::KVStore store;
    store.set("k1", "v1");
    store.set("k2", "v2");
    store.set("k3", "v3");

    size_t deleted = store.del(std::vector<std::string>{"k1", "k2", "k3", "k4_nonexistent"});
    check(deleted == 3, "bulk del({k1,k2,k3,k4_nonexistent}) returns 3 (count of actually deleted)");
}

void test_keys() {
    std::cout << "\n[Test: Keys]" << std::endl;
    kvstore::KVStore store;
    store.set("alpha", "1");
    store.set("beta", "2");
    store.set("gamma", "3");

    auto all_keys = store.keys();
    check(all_keys.size() == 3, "keys() returns 3 keys");

    // Check all expected keys are present
    bool has_alpha = std::find(all_keys.begin(), all_keys.end(), "alpha") != all_keys.end();
    bool has_beta = std::find(all_keys.begin(), all_keys.end(), "beta") != all_keys.end();
    bool has_gamma = std::find(all_keys.begin(), all_keys.end(), "gamma") != all_keys.end();
    check(has_alpha && has_beta && has_gamma, "keys() contains all current keys");
}

void test_stats() {
    std::cout << "\n[Test: Stats]" << std::endl;
    kvstore::KVStore store;

    store.set("a", "1");
    store.set("b", "2");
    check(store.totalSets() == 2, "totalSets increments correctly (2 after 2 sets)");

    store.get("a");   // hit
    store.get("b");   // hit
    store.get("zzz"); // miss
    check(store.totalGets() == 3, "totalGets increments correctly (3 after 3 gets)");
    check(store.totalHits() == 2, "totalHits increments correctly (2 hits)");
    check(store.totalMisses() == 1, "totalMisses increments correctly (1 miss)");
}

void test_thread_safety() {
    std::cout << "\n[Test: Thread safety]" << std::endl;
    kvstore::KVStore store(10000);

    // Phase 1: concurrent SET operations
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < 10; t++) {
            threads.emplace_back([&store, t]() {
                for (int i = 0; i < 100; i++) {
                    std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                    store.set(key, "value");
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    check(store.size() == 1000, "after 10 threads x 100 SETs on unique keys, size == 1000");

    // Phase 2: concurrent GET operations (all keys exist, so all should be hits)
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < 10; t++) {
            threads.emplace_back([&store, t]() {
                for (int i = 0; i < 100; i++) {
                    std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                    store.get(key);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    check(store.totalHits() == 1000, "after 10 threads x 100 GETs on existing keys, totalHits == 1000");
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  KVStore Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_basic_set_and_get();
    test_get_nonexistent();
    test_delete();
    test_get_after_delete();
    test_exists();
    test_set_overwrites();
    test_ttl_expiry();
    test_ttl_command_with_expiry();
    test_ttl_command_no_expiry();
    test_ttl_command_nonexistent();
    test_lru_eviction();
    test_lru_access_pattern();
    test_bulk_delete();
    test_keys();
    test_stats();
    test_thread_safety();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
