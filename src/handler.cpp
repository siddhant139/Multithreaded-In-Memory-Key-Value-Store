#include "handler.h"
#include "store.h"
#include "parser.h"
#include "persistence.h"
#include "resp.h"

#include <sstream>

namespace kvstore {

CommandHandler::CommandHandler(KVStore& store)
    : store_(store), start_time_(std::chrono::steady_clock::now()) {}

Reply CommandHandler::execute(const Command& cmd) {
    if (cmd.name.empty()) {
        return Reply::error("empty command");
    }
    // Validate command name + minimum argument count.
    if (!Parser::validate(cmd)) {
        return Reply::error("unknown command or wrong number of arguments for '" + cmd.name + "'");
    }

    if (cmd.name == "SET")    return handleSet(cmd);
    if (cmd.name == "GET")    return handleGet(cmd);
    if (cmd.name == "DEL")    return handleDel(cmd);
    if (cmd.name == "EXISTS") return handleExists(cmd);
    if (cmd.name == "KEYS")   return handleKeys(cmd);
    if (cmd.name == "TTL")    return handleTtl(cmd);
    if (cmd.name == "INFO")   return handleInfo(cmd);
    if (cmd.name == "PING")   return handlePing(cmd);

    return Reply::error("unknown command '" + cmd.name + "'");
}

Reply CommandHandler::handleSet(const Command& cmd) {
    // SET key value [EX seconds]
    std::optional<int> ttl;

    if (cmd.args.size() >= 4 && (cmd.args[2] == "EX" || cmd.args[2] == "ex")) {
        try {
            ttl = std::stoi(cmd.args[3]);
            if (ttl.value() <= 0) {
                return Reply::error("invalid expire time");
            }
        } catch (...) {
            return Reply::error("invalid expire time");
        }
    }

    store_.set(cmd.args[0], cmd.args[1], ttl);
    if (aof_) aof_->logWrite(Resp::encode(cmd));  // persist the write
    return Reply::simple("OK");
}

Reply CommandHandler::handleGet(const Command& cmd) {
    auto value = store_.get(cmd.args[0]);
    if (value.has_value()) {
        return Reply::bulk(value.value());
    }
    return Reply::nil();
}

Reply CommandHandler::handleDel(const Command& cmd) {
    if (cmd.args.size() == 1) {
        bool deleted = store_.del(cmd.args[0]);
        if (deleted && aof_) aof_->logWrite(Resp::encode(cmd));  // persist only real deletes
        return Reply::integ(deleted ? 1 : 0);
    }

    size_t deleted = store_.del(cmd.args);
    if (deleted > 0 && aof_) aof_->logWrite(Resp::encode(cmd));
    return Reply::integ(static_cast<int64_t>(deleted));
}

Reply CommandHandler::handleExists(const Command& cmd) {
    bool found = store_.exists(cmd.args[0]);
    return Reply::integ(found ? 1 : 0);
}

Reply CommandHandler::handleKeys(const Command& /*cmd*/) {
    auto all_keys = store_.keys();
    std::vector<Reply> elements;
    elements.reserve(all_keys.size());
    for (const auto& key : all_keys) {
        elements.push_back(Reply::bulk(key));
    }
    return Reply::array(std::move(elements));
}

Reply CommandHandler::handleTtl(const Command& cmd) {
    auto remaining = store_.ttl(cmd.args[0]);
    if (!remaining.has_value()) {
        return Reply::integ(-2);  // key doesn't exist
    }
    return Reply::integ(remaining.value());
}

Reply CommandHandler::handleInfo(const Command& /*cmd*/) {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_);

    std::ostringstream oss;
    oss << "# Server\n";
    oss << "uptime_seconds:" << uptime.count() << "\n";
    oss << "# Keyspace\n";
    oss << "keys:" << store_.size() << "\n";
    oss << "max_keys:" << store_.maxEntries() << "\n";
    oss << "# Stats\n";
    oss << "total_gets:" << store_.totalGets() << "\n";
    oss << "total_sets:" << store_.totalSets() << "\n";
    oss << "total_hits:" << store_.totalHits() << "\n";
    oss << "total_misses:" << store_.totalMisses() << "\n";
    oss << "total_evictions:" << store_.totalEvictions();
    return Reply::bulk(oss.str());
}

Reply CommandHandler::handlePing(const Command& /*cmd*/) {
    return Reply::simple("PONG");
}

} // namespace kvstore
