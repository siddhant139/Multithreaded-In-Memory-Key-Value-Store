#pragma once

#include <thread>
#include <atomic>
#include <chrono>

namespace kvstore {

class KVStore;

// Background thread that periodically samples and purges expired keys
// Uses Redis-style probabilistic expiry: sample N random keys per cycle,
// delete expired ones. If >25% were expired, repeat immediately.
class ExpiryManager {
public:
    ExpiryManager(KVStore& store,
                  std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    ~ExpiryManager();

    // Non-copyable
    ExpiryManager(const ExpiryManager&) = delete;
    ExpiryManager& operator=(const ExpiryManager&) = delete;

    void start();
    void stop();

    uint64_t totalPurged() const;

private:
    KVStore& store_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_;
    std::thread worker_;
    std::atomic<uint64_t> total_purged_{0};

    void purgeLoop();
};

} // namespace kvstore
