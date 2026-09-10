#pragma once

#include "types.h"
#include "reply.h"
#include <chrono>

namespace kvstore {

class KVStore;
class AofPersistence;

// Routes parsed commands to the appropriate store operations.
// Returns a structured Reply, which the server serializes into RESP or inline
// text depending on the client's protocol.
class CommandHandler {
public:
    explicit CommandHandler(KVStore& store);

    // Execute a parsed command and produce a structured reply.
    // Validation (unknown command / arity) is handled here and surfaced as an
    // error Reply.
    Reply execute(const Command& cmd);

    // Attach an append-only-file for persistence. When set, successful write
    // commands (SET/DEL) are logged. Leave null (the default) during replay so
    // replayed commands are not re-logged.
    void setAof(AofPersistence* aof) { aof_ = aof; }

private:
    KVStore& store_;
    AofPersistence* aof_ = nullptr;
    std::chrono::steady_clock::time_point start_time_;

    Reply handleSet(const Command& cmd);
    Reply handleGet(const Command& cmd);
    Reply handleDel(const Command& cmd);
    Reply handleExists(const Command& cmd);
    Reply handleKeys(const Command& cmd);
    Reply handleTtl(const Command& cmd);
    Reply handleInfo(const Command& cmd);
    Reply handlePing(const Command& cmd);
};

} // namespace kvstore
