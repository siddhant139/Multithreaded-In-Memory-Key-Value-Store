#include "server.h"
#include "thread_pool.h"
#include "parser.h"
#include "resp.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>

namespace kvstore {

TCPServer::TCPServer(int port, ThreadPool& pool, RequestHandler handler)
    : port_(port), server_fd_(-1), running_(false),
      pool_(pool), handler_(std::move(handler)) {}

TCPServer::~TCPServer() {
    stop();
}

void TCPServer::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port_));
    }

    if (listen(server_fd_, SOMAXCONN) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to listen");
    }

    running_.store(true);
    std::cout << "[kvstore] Server listening on port " << port_ << std::endl;

    acceptLoop();
}

void TCPServer::stop() {
    running_.store(false);
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    // Close all client connections
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        close(fd);
    }
    client_fds_.clear();
}

size_t TCPServer::connectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return client_fds_.size();
}

void TCPServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);

        if (client_fd < 0) {
            if (running_.load()) {
                std::cerr << "[kvstore] Accept failed: " << strerror(errno) << std::endl;
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.insert(client_fd);
        }

        // Submit client handling to thread pool
        pool_.submit([this, client_fd]() {
            handleClient(client_fd);
        });
    }
}

void TCPServer::handleClient(int client_fd) {
    char chunk[4096];
    std::string buf;  // accumulates bytes across recv() calls (TCP is a stream)

    while (running_.load()) {
        ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk), 0);
        if (bytes_read <= 0) {
            break;  // client disconnected or error
        }
        buf.append(chunk, static_cast<size_t>(bytes_read));

        // Drain every complete command currently buffered. A single recv() may
        // contain several pipelined commands, or only part of one.
        bool fatal = false;
        while (!buf.empty()) {
            if (buf[0] == '*') {
                // ---- RESP protocol (e.g. redis-cli) ----
                long flen = Resp::frameLength(buf);
                if (flen < 0) {
                    // Malformed RESP: report and drop the connection.
                    std::string err = Reply::error("Protocol error").toResp();
                    send(client_fd, err.c_str(), err.size(), 0);
                    fatal = true;
                    break;
                }
                if (flen == 0) break;  // incomplete; wait for more bytes

                std::string frame = buf.substr(0, static_cast<size_t>(flen));
                buf.erase(0, static_cast<size_t>(flen));

                Command cmd = Resp::parse(frame);
                Reply reply = handler_(cmd);
                std::string out = reply.toResp();
                if (send(client_fd, out.c_str(), out.size(), 0) < 0) { fatal = true; break; }
            } else {
                // ---- Inline text protocol (e.g. nc / telnet) ----
                size_t nl = buf.find('\n');
                if (nl == std::string::npos) break;  // incomplete line; wait for more

                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();  // strip CR from CRLF line endings
                }
                if (line.empty()) continue;

                Command cmd = Parser::parse(line);
                Reply reply = handler_(cmd);
                std::string out = reply.toInline() + "\n";
                if (send(client_fd, out.c_str(), out.size(), 0) < 0) { fatal = true; break; }
            }
        }

        if (fatal) break;
    }

    removeClient(client_fd);
}

void TCPServer::removeClient(int client_fd) {
    close(client_fd);
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.erase(client_fd);
}

} // namespace kvstore
