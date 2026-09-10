#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>
#include <functional>

namespace kvstore {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

struct Entry {
    std::string value;
    TimePoint created_at;
    std::optional<TimePoint> expires_at;
};

struct Command {
    std::string name;
    std::vector<std::string> args;
};

constexpr int DEFAULT_PORT = 6379;
constexpr size_t DEFAULT_MAX_ENTRIES = 100000;
constexpr size_t DEFAULT_THREAD_POOL_SIZE = 4;
constexpr int EXPIRY_CHECK_INTERVAL_MS = 1000;

} // namespace kvstore
