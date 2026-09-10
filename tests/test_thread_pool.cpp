#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <memory>

#include "../src/thread_pool.h"

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

void test_basic_task_execution() {
    std::cout << "\n[Test: Basic task execution]" << std::endl;
    kvstore::ThreadPool pool(2);
    std::atomic<int> value{0};

    pool.submit([&value]() {
        value.store(42);
    });

    // Give the task time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    check(value.load() == 42, "submitted task sets atomic<int> to 42");
}

void test_multiple_tasks() {
    std::cout << "\n[Test: Multiple tasks]" << std::endl;
    kvstore::ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; i++) {
        pool.submit([&counter]() {
            counter.fetch_add(1);
        });
    }

    // Give tasks time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    check(counter.load() == 100, "100 tasks each incrementing atomic<int>, final value is 100");
}

void test_pool_size() {
    std::cout << "\n[Test: Pool size]" << std::endl;
    kvstore::ThreadPool pool(4);
    check(pool.poolSize() == 4, "pool created with 4 threads, poolSize() returns 4");
}

void test_concurrent_execution() {
    std::cout << "\n[Test: Concurrent execution]" << std::endl;
    kvstore::ThreadPool pool(4);
    std::atomic<int> completed{0};

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 4; i++) {
        pool.submit([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            completed.fetch_add(1);
        });
    }

    // Wait for all tasks to finish
    while (completed.load() < 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "    (elapsed: " << elapsed_ms << "ms)" << std::endl;

    // If truly parallel, ~100ms. If sequential, ~400ms. Allow generous margin.
    check(elapsed_ms < 300, "4 tasks sleeping 100ms each complete in < 300ms (proves parallelism)");
}

void test_stress() {
    std::cout << "\n[Test: Stress test]" << std::endl;
    kvstore::ThreadPool pool(8);
    std::atomic<int> counter{0};

    for (int i = 0; i < 10000; i++) {
        pool.submit([&counter]() {
            counter.fetch_add(1);
        });
    }

    // Give tasks time to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));

    check(counter.load() == 10000, "10000 tasks incrementing atomic counter, final value is 10000");
}

void test_tasks_complete_before_destructor() {
    std::cout << "\n[Test: Tasks complete before destructor]" << std::endl;
    std::atomic<int> counter{0};

    {
        kvstore::ThreadPool pool(4);
        for (int i = 0; i < 100; i++) {
            pool.submit([&counter]() {
                counter.fetch_add(1);
            });
        }
        // pool goes out of scope here — destructor should wait for all tasks
    }

    check(counter.load() == 100, "all 100 tasks completed after pool destructor runs");
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ThreadPool Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_basic_task_execution();
    test_multiple_tasks();
    test_pool_size();
    test_concurrent_execution();
    test_stress();
    test_tasks_complete_before_destructor();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
