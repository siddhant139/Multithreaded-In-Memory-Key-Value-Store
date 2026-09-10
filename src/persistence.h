#pragma once

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <cstddef>

namespace kvstore {

// When the OS actually flushes buffered writes to disk.
//   ALWAYS   - fsync after every write. Zero data loss on crash, but slowest.
//   EVERYSEC - fsync once per second on a background thread (Redis default).
//              At most ~1s of writes lost on a crash; good durability/throughput balance.
//   NO       - never explicitly fsync; let the OS decide. Fastest, weakest durability.
enum class FsyncPolicy { ALWAYS, EVERYSEC, NO };

FsyncPolicy parseFsyncPolicy(const std::string& s);
const char* fsyncPolicyName(FsyncPolicy p);

// Append-Only File persistence.
//
// Every write command (SET/DEL) is appended to a log file in RESP format. On
// startup the log is replayed to reconstruct the dataset, giving crash recovery
// for an otherwise in-memory store. This is the same durability mechanism Redis
// uses (its AOF).
class AofPersistence {
public:
    AofPersistence(const std::string& path, FsyncPolicy policy);
    ~AofPersistence();

    AofPersistence(const AofPersistence&) = delete;
    AofPersistence& operator=(const AofPersistence&) = delete;

    // Open the file for appending (creating it if needed). Returns false on error.
    bool open();

    // Append one already-RESP-encoded command and apply the fsync policy.
    void logWrite(const std::string& resp_command);

    // Replay the existing file: for each complete RESP command, invoke applyLine
    // with its raw bytes. Returns the number of commands replayed.
    size_t replay(const std::function<void(const std::string&)>& applyCommand);

    // Start/stop the background fsync thread (only used for EVERYSEC).
    void start();
    void stop();

    uint64_t commandsLogged() const { return commands_logged_.load(std::memory_order_relaxed); }

private:
    std::string path_;
    FsyncPolicy policy_;
    int fd_;                              // append file descriptor
    std::mutex write_mutex_;              // serializes appends from handler threads
    std::atomic<bool> dirty_;             // unflushed writes pending (for EVERYSEC)
    std::atomic<bool> running_;
    std::thread fsync_thread_;
    std::atomic<uint64_t> commands_logged_{0};

    void fsyncLoop();
};

} // namespace kvstore
