#include "expiry_manager.h"
#include "store.h"

namespace kvstore {

ExpiryManager::ExpiryManager(KVStore& store, std::chrono::milliseconds interval)
    : store_(store), interval_(interval), running_(false) {}

ExpiryManager::~ExpiryManager() {
    stop();
}

void ExpiryManager::start() {
    running_.store(true);
    worker_ = std::thread(&ExpiryManager::purgeLoop, this);
}

void ExpiryManager::stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

uint64_t ExpiryManager::totalPurged() const {
    return total_purged_.load(std::memory_order_relaxed);
}

void ExpiryManager::purgeLoop() {
    while (running_.load()) {
        size_t purged = store_.purgeExpired(20);  // sample 20 keys per cycle
        total_purged_.fetch_add(purged, std::memory_order_relaxed);

        std::this_thread::sleep_for(interval_);
    }
}

} // namespace kvstore
