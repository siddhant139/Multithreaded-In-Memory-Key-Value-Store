#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

static int tests_run = 0;
static int tests_passed = 0;

void check(const std::string& name, bool condition) {
    tests_run++;
    if (condition) {
        tests_passed++;
        std::cout << "  PASS: " << name << std::endl;
    } else {
        std::cout << "  FAIL: " << name << std::endl;
    }
}

class TestClient {
public:
    TestClient(const std::string& host, int port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        connected_ = (connect(fd_, (sockaddr*)&addr, sizeof(addr)) == 0);
    }
    ~TestClient() { if (fd_ >= 0) close(fd_); }
    bool isConnected() const { return connected_; }
    std::string sendCommand(const std::string& cmd) {
        std::string full = cmd + "\r\n";
        send(fd_, full.c_str(), full.size(), 0);
        char buf[4096];
        // small delay to let server process
        usleep(10000);
        ssize_t n = recv(fd_, buf, sizeof(buf)-1, 0);
        if (n <= 0) return "";
        buf[n] = '\0';
        std::string resp(buf);
        // trim trailing newline
        while (!resp.empty() && (resp.back() == '\n' || resp.back() == '\r')) resp.pop_back();
        return resp;
    }
private:
    int fd_ = -1;
    bool connected_ = false;
};

void test_connection(const std::string& host, int port) {
    std::cout << "\n[Test: Connection]" << std::endl;
    TestClient client(host, port);
    check("Client connects to server", client.isConnected());
}

void test_ping(const std::string& host, int port) {
    std::cout << "\n[Test: PING]" << std::endl;
    TestClient client(host, port);
    std::string resp = client.sendCommand("PING");
    check("PING returns PONG", resp == "PONG");
}

void test_set_get(const std::string& host, int port) {
    std::cout << "\n[Test: SET/GET]" << std::endl;
    TestClient client(host, port);
    std::string resp = client.sendCommand("SET foo bar");
    check("SET foo bar returns OK", resp == "OK");
    resp = client.sendCommand("GET foo");
    check("GET foo returns bar", resp == "bar");
}

void test_get_nonexistent(const std::string& host, int port) {
    std::cout << "\n[Test: GET non-existent key]" << std::endl;
    TestClient client(host, port);
    std::string resp = client.sendCommand("GET nonexistent");
    check("GET nonexistent returns (nil)", resp == "(nil)");
}

void test_del(const std::string& host, int port) {
    std::cout << "\n[Test: DEL]" << std::endl;
    TestClient client(host, port);
    client.sendCommand("SET key1 val1");
    std::string resp = client.sendCommand("DEL key1");
    check("DEL key1 returns (integer) 1", resp == "(integer) 1");
    resp = client.sendCommand("GET key1");
    check("GET key1 after DEL returns (nil)", resp == "(nil)");
}

void test_exists(const std::string& host, int port) {
    std::cout << "\n[Test: EXISTS]" << std::endl;
    TestClient client(host, port);
    client.sendCommand("SET ex1 val");
    std::string resp = client.sendCommand("EXISTS ex1");
    check("EXISTS ex1 returns (integer) 1", resp == "(integer) 1");
    resp = client.sendCommand("EXISTS nokey");
    check("EXISTS nokey returns (integer) 0", resp == "(integer) 0");
}

void test_ttl(const std::string& host, int port) {
    std::cout << "\n[Test: SET with TTL]" << std::endl;
    TestClient client(host, port);
    std::string resp = client.sendCommand("SET ttlkey val EX 2");
    check("SET ttlkey with EX 2 returns OK", resp == "OK");
    resp = client.sendCommand("GET ttlkey");
    check("GET ttlkey before expiry returns val", resp == "val");
    std::cout << "  Waiting 3 seconds for TTL expiry..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    resp = client.sendCommand("GET ttlkey");
    check("GET ttlkey after expiry returns (nil)", resp == "(nil)");
}

void test_info(const std::string& host, int port) {
    std::cout << "\n[Test: INFO]" << std::endl;
    TestClient client(host, port);
    std::string resp = client.sendCommand("INFO");
    check("INFO contains uptime_seconds", resp.find("uptime_seconds") != std::string::npos);
    check("INFO contains keys", resp.find("keys") != std::string::npos);
}

void test_multi_client(const std::string& host, int port) {
    std::cout << "\n[Test: Multi-client concurrency]" << std::endl;

    const int num_threads = 5;
    const int ops_per_thread = 20;
    std::atomic<int> failures(0);
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&, i]() {
            TestClient client(host, port);
            if (!client.isConnected()) {
                failures++;
                return;
            }
            for (int j = 0; j < ops_per_thread; j++) {
                std::string key = "thread_" + std::to_string(i) + "_key_" + std::to_string(j);
                std::string val = "val_" + std::to_string(i) + "_" + std::to_string(j);
                client.sendCommand("SET " + key + " " + val);
                std::string resp = client.sendCommand("GET " + key);
                if (resp != val) {
                    failures++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    check("All multi-client SET/GET operations correct (" +
          std::to_string(num_threads) + " threads x " +
          std::to_string(ops_per_thread) + " ops)",
          failures.load() == 0);
}

int main(int argc, char* argv[]) {
    int port = 6399;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::cout << "Integration tests - connect to localhost:" << port << std::endl;
    std::cout << "Start server first: ./build/kvstore -p " << port << std::endl;
    std::cout << "(Eviction not tested here - requires server started with -m flag)" << std::endl;

    // Verify connection before running tests
    {
        TestClient probe("127.0.0.1", port);
        if (!probe.isConnected()) {
            std::cerr << "ERROR: Cannot connect to server on localhost:" << port << std::endl;
            std::cerr << "Make sure the server is running: ./build/kvstore -p " << port << std::endl;
            return 1;
        }
    }

    test_connection("127.0.0.1", port);
    test_ping("127.0.0.1", port);
    test_set_get("127.0.0.1", port);
    test_get_nonexistent("127.0.0.1", port);
    test_del("127.0.0.1", port);
    test_exists("127.0.0.1", port);
    test_ttl("127.0.0.1", port);
    test_info("127.0.0.1", port);
    test_multi_client("127.0.0.1", port);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << "/" << tests_run << " tests passed" << std::endl;
    if (tests_passed == tests_run) {
        std::cout << "ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "SOME TESTS FAILED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
