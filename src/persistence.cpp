#include "persistence.h"
#include "resp.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>

namespace kvstore {

FsyncPolicy parseFsyncPolicy(const std::string& s) {
    if (s == "always")   return FsyncPolicy::ALWAYS;
    if (s == "no")       return FsyncPolicy::NO;
    return FsyncPolicy::EVERYSEC;  // default
}

const char* fsyncPolicyName(FsyncPolicy p) {
    switch (p) {
        case FsyncPolicy::ALWAYS:   return "always";
        case FsyncPolicy::EVERYSEC: return "everysec";
        case FsyncPolicy::NO:       return "no";
    }
    return "everysec";
}

AofPersistence::AofPersistence(const std::string& path, FsyncPolicy policy)
    : path_(path), policy_(policy), fd_(-1), dirty_(false), running_(false) {}

AofPersistence::~AofPersistence() {
    stop();
    if (fd_ >= 0) {
        fsync(fd_);
        close(fd_);
        fd_ = -1;
    }
}

bool AofPersistence::open() {
    // O_APPEND guarantees each write atomically lands at end-of-file.
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[kvstore] AOF: cannot open " << path_ << ": " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void AofPersistence::logWrite(const std::string& resp_command) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ < 0) return;

    ssize_t written = ::write(fd_, resp_command.data(), resp_command.size());
    if (written < 0) {
        std::cerr << "[kvstore] AOF write failed: " << strerror(errno) << std::endl;
        return;
    }
    commands_logged_.fetch_add(1, std::memory_order_relaxed);

    if (policy_ == FsyncPolicy::ALWAYS) {
        fsync(fd_);                       // durable before we return
    } else if (policy_ == FsyncPolicy::EVERYSEC) {
        dirty_.store(true, std::memory_order_relaxed);  // background thread flushes
    }
    // NO: leave it to the OS.
}

size_t AofPersistence::replay(const std::function<void(const std::string&)>& applyCommand) {
    // Read the whole file, then repeatedly frame + hand off one RESP command.
    std::ifstream in(path_, std::ios::binary);
    if (!in) return 0;  // no file yet -> nothing to replay

    std::stringstream ss;
    ss << in.rdbuf();
    std::string buf = ss.str();

    size_t count = 0;
    size_t pos = 0;
    while (pos < buf.size()) {
        std::string remaining = buf.substr(pos);
        long len = Resp::frameLength(remaining);
        if (len <= 0) break;  // incomplete/garbage tail (e.g. torn write on crash) -> stop
        applyCommand(remaining.substr(0, static_cast<size_t>(len)));
        pos += static_cast<size_t>(len);
        ++count;
    }
    return count;
}

void AofPersistence::start() {
    if (policy_ != FsyncPolicy::EVERYSEC) return;  // only EVERYSEC needs the thread
    running_.store(true);
    fsync_thread_ = std::thread(&AofPersistence::fsyncLoop, this);
}

void AofPersistence::stop() {
    running_.store(false);
    if (fsync_thread_.joinable()) fsync_thread_.join();
}

void AofPersistence::fsyncLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (dirty_.exchange(false)) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (fd_ >= 0) fsync(fd_);
        }
    }
}

} // namespace kvstore
