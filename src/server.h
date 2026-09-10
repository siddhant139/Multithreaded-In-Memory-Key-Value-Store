#pragma once

#include "types.h"
#include "reply.h"
#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace kvstore {

class ThreadPool;

class TCPServer {
public:
    // The server parses the wire bytes into a Command (inline or RESP), invokes
    // this callback to get a structured Reply, then serializes the Reply back in
    // the client's protocol.
    using RequestHandler = std::function<Reply(const Command&)>;

    TCPServer(int port, ThreadPool& pool, RequestHandler handler);
    ~TCPServer();

    // Non-copyable
    TCPServer(const TCPServer&) = delete;
    TCPServer& operator=(const TCPServer&) = delete;

    void start();  // blocking: runs accept loop
    void stop();

    size_t connectedClients() const;

private:
    int port_;
    int server_fd_;
    std::atomic<bool> running_;
    ThreadPool& pool_;
    RequestHandler handler_;

    mutable std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;

    void acceptLoop();
    void handleClient(int client_fd);
    void removeClient(int client_fd);
};

} // namespace kvstore
