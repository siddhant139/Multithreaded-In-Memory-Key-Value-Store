#include "server.h"
#include "thread_pool.h"
#include "store.h"
#include "handler.h"
#include "parser.h"
#include "resp.h"
#include "persistence.h"
#include "expiry_manager.h"
#include "types.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>

static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\n[kvstore] Shutting down (signal " << signum << ")..." << std::endl;
    g_running.store(false);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -p PORT       Port number (default: 6379)\n"
              << "  -t THREADS    Thread pool size (default: 4)\n"
              << "  -m MAX        Max entries before LRU eviction (default: 100000)\n"
              << "  --aof FILE    Enable append-only-file persistence at FILE\n"
              << "  --fsync POL   AOF fsync policy: always | everysec | no (default: everysec)\n"
              << "  -h            Show this help\n";
}

int main(int argc, char* argv[]) {
    int port = kvstore::DEFAULT_PORT;
    size_t threads = kvstore::DEFAULT_THREAD_POOL_SIZE;
    size_t max_entries = kvstore::DEFAULT_MAX_ENTRIES;
    std::string aof_path;
    kvstore::FsyncPolicy fsync_policy = kvstore::FsyncPolicy::EVERYSEC;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "-t" && i + 1 < argc) {
            threads = std::stoul(argv[++i]);
        } else if (arg == "-m" && i + 1 < argc) {
            max_entries = std::stoul(argv[++i]);
        } else if (arg == "--aof" && i + 1 < argc) {
            aof_path = argv[++i];
        } else if (arg == "--fsync" && i + 1 < argc) {
            fsync_policy = kvstore::parseFsyncPolicy(argv[++i]);
        } else if (arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "[kvstore] Multithreaded In-Memory Key-Value Store\n";
    std::cout << "[kvstore] Port: " << port << "\n";
    std::cout << "[kvstore] Thread pool: " << threads << " workers\n";
    std::cout << "[kvstore] Max entries: " << max_entries << "\n";

    // Initialize components
    kvstore::KVStore store(max_entries);
    kvstore::CommandHandler handler(store);
    kvstore::ThreadPool pool(threads);
    kvstore::ExpiryManager expiry(store,
        std::chrono::milliseconds(kvstore::EXPIRY_CHECK_INTERVAL_MS));

    // ---- Persistence (AOF): recover state, then attach for live logging ----
    std::unique_ptr<kvstore::AofPersistence> aof;
    if (!aof_path.empty()) {
        aof = std::make_unique<kvstore::AofPersistence>(aof_path, fsync_policy);
        std::cout << "[kvstore] AOF: " << aof_path
                  << " (fsync=" << kvstore::fsyncPolicyName(fsync_policy) << ")\n";

        // 1) Replay any existing log to rebuild the dataset. The handler's AOF
        //    is still null here, so replayed writes are NOT re-logged.
        size_t replayed = aof->replay([&](const std::string& resp_cmd) {
            kvstore::Command cmd = kvstore::Resp::parse(resp_cmd);
            handler.execute(cmd);
        });
        if (replayed > 0) {
            std::cout << "[kvstore] AOF: recovered " << replayed
                      << " commands, " << store.size() << " keys\n";
        }

        // 2) Open for appending and attach so future writes are persisted.
        if (!aof->open()) {
            std::cerr << "[kvstore] Fatal: could not open AOF for writing\n";
            return 1;
        }
        aof->start();               // background fsync thread (EVERYSEC)
        handler.setAof(aof.get());  // live writes now logged
    }

    // Start background expiry thread
    expiry.start();

    // Create and start TCP server. The server handles wire framing/parsing for
    // both inline and RESP protocols, and calls this callback with a parsed
    // Command to obtain a structured Reply.
    kvstore::TCPServer server(port, pool,
        [&handler](const kvstore::Command& cmd) -> kvstore::Reply {
            return handler.execute(cmd);
        });

    try {
        server.start();  // Blocking — runs accept loop
    } catch (const std::exception& e) {
        std::cerr << "[kvstore] Fatal: " << e.what() << std::endl;
        return 1;
    }

    // Cleanup
    expiry.stop();
    if (aof) aof->stop();
    std::cout << "[kvstore] Server stopped." << std::endl;

    return 0;
}
