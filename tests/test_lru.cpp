// Standalone test for kvstore::LRUEvictionPolicy
// Compile: g++ -std=c++17 -I../src test_lru.cpp ../src/lru_cache.cpp -o test_lru

#include <cassert>
#include <iostream>
#include <string>
#include <optional>

#include "../src/lru_cache.h"

using kvstore::LRUEvictionPolicy;

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_basic_insert_and_size() {
    LRUEvictionPolicy lru(5);
    check(lru.size() == 0, "Initial size is 0");

    lru.recordInsert("a");
    check(lru.size() == 1, "Size is 1 after inserting 'a'");

    lru.recordInsert("b");
    check(lru.size() == 2, "Size is 2 after inserting 'b'");

    lru.recordInsert("c");
    check(lru.size() == 3, "Size is 3 after inserting 'c'");
}

void test_eviction_returns_lru_key() {
    LRUEvictionPolicy lru(3);
    lru.recordInsert("a");
    lru.recordInsert("b");
    lru.recordInsert("c");

    // Insert d — but eviction is caller's responsibility; just test evict()
    lru.recordInsert("d");
    // Now order front->back: d, c, b, a — evict should return 'a' (least recent)
    auto evicted = lru.evict();
    check(evicted.has_value(), "Evict returns a value");
    check(evicted.value() == "a", "Evict returns LRU key 'a' (first inserted, never accessed)");
}

void test_access_moves_to_front() {
    LRUEvictionPolicy lru(3);
    lru.recordInsert("a");  // order: a
    lru.recordInsert("b");  // order: b, a
    lru.recordInsert("c");  // order: c, b, a

    // Access 'a' — moves it to front
    lru.recordAccess("a");  // order: a, c, b

    // Evict should return 'b' (now the least recently used), not 'a'
    auto evicted = lru.evict();
    check(evicted.has_value(), "Access test - evict returns a value");
    check(evicted.value() == "b", "Access test - evict returns 'b' (LRU after 'a' was accessed)");
}

void test_delete_removes_from_tracking() {
    LRUEvictionPolicy lru(5);
    lru.recordInsert("a");
    lru.recordInsert("b");
    lru.recordInsert("c");
    check(lru.size() == 3, "Delete test - size is 3 before delete");

    lru.recordDelete("b");
    check(lru.size() == 2, "Delete test - size is 2 after deleting 'b'");

    // Evict should return 'a' (oldest), not 'b' (deleted)
    auto evicted = lru.evict();
    check(evicted.has_value(), "Delete test - evict returns a value");
    check(evicted.value() == "a", "Delete test - evict returns 'a' (oldest remaining)");
}

void test_evict_on_empty_returns_nullopt() {
    LRUEvictionPolicy lru(3);
    auto evicted = lru.evict();
    check(!evicted.has_value(), "Evict on empty returns nullopt");
}

void test_should_evict_at_capacity() {
    LRUEvictionPolicy lru(3);
    lru.recordInsert("a");
    lru.recordInsert("b");
    check(!lru.shouldEvict(), "shouldEvict returns false when below capacity (2/3)");

    lru.recordInsert("c");
    check(lru.shouldEvict(), "shouldEvict returns true when at capacity (3/3)");
}

void test_insert_duplicate_acts_as_access() {
    LRUEvictionPolicy lru(5);
    lru.recordInsert("a");
    lru.recordInsert("b");
    lru.recordInsert("c");
    check(lru.size() == 3, "Duplicate test - size is 3 before duplicate insert");

    // Insert 'a' again — should act as access, not increase size
    lru.recordInsert("a");
    check(lru.size() == 3, "Duplicate test - size remains 3 after duplicate insert of 'a'");

    // Order should now be: a, c, b — evict should return 'b'
    auto evicted = lru.evict();
    check(evicted.has_value(), "Duplicate test - evict returns a value");
    check(evicted.value() == "b", "Duplicate test - evict returns 'b' (LRU after 'a' re-inserted)");
}

void test_multiple_evictions_in_sequence() {
    LRUEvictionPolicy lru(5);
    lru.recordInsert("a");  // order: a
    lru.recordInsert("b");  // order: b, a
    lru.recordInsert("c");  // order: c, b, a
    lru.recordInsert("d");  // order: d, c, b, a
    lru.recordInsert("e");  // order: e, d, c, b, a

    // Evictions should come out in LRU order: a, b, c, d, e
    auto e1 = lru.evict();
    check(e1.has_value() && e1.value() == "a", "Sequential evict #1 returns 'a'");

    auto e2 = lru.evict();
    check(e2.has_value() && e2.value() == "b", "Sequential evict #2 returns 'b'");

    auto e3 = lru.evict();
    check(e3.has_value() && e3.value() == "c", "Sequential evict #3 returns 'c'");

    auto e4 = lru.evict();
    check(e4.has_value() && e4.value() == "d", "Sequential evict #4 returns 'd'");

    auto e5 = lru.evict();
    check(e5.has_value() && e5.value() == "e", "Sequential evict #5 returns 'e'");

    // Now empty
    auto e6 = lru.evict();
    check(!e6.has_value(), "Sequential evict #6 on empty returns nullopt");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== LRU Eviction Policy Tests ===" << std::endl;

    std::cout << "\n-- Basic insert and size --" << std::endl;
    test_basic_insert_and_size();

    std::cout << "\n-- Eviction returns LRU key --" << std::endl;
    test_eviction_returns_lru_key();

    std::cout << "\n-- Access moves key to front --" << std::endl;
    test_access_moves_to_front();

    std::cout << "\n-- Delete removes from tracking --" << std::endl;
    test_delete_removes_from_tracking();

    std::cout << "\n-- Evict on empty --" << std::endl;
    test_evict_on_empty_returns_nullopt();

    std::cout << "\n-- shouldEvict at capacity --" << std::endl;
    test_should_evict_at_capacity();

    std::cout << "\n-- Insert duplicate acts as access --" << std::endl;
    test_insert_duplicate_acts_as_access();

    std::cout << "\n-- Multiple evictions in sequence --" << std::endl;
    test_multiple_evictions_in_sequence();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===" << std::endl;

    return (tests_failed > 0) ? 1 : 0;
}
