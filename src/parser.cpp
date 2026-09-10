#include "parser.h"
#include <sstream>
#include <algorithm>

namespace kvstore {

Command Parser::parse(const std::string& raw_input) {
    Command cmd;
    std::istringstream stream(raw_input);
    std::string token;

    // First token is the command name
    if (stream >> token) {
        // Convert command name to uppercase
        std::transform(token.begin(), token.end(), token.begin(), ::toupper);
        cmd.name = token;
    }

    // Remaining tokens are arguments
    while (stream >> token) {
        cmd.args.push_back(token);
    }

    return cmd;
}

bool Parser::validate(const Command& cmd) {
    if (cmd.name.empty()) return false;

    if (cmd.name == "SET") return cmd.args.size() >= 2;   // SET key value [EX seconds]
    if (cmd.name == "GET") return cmd.args.size() >= 1;   // GET key
    if (cmd.name == "DEL") return cmd.args.size() >= 1;   // DEL key [key ...]
    if (cmd.name == "EXISTS") return cmd.args.size() >= 1; // EXISTS key
    if (cmd.name == "KEYS") return true;                   // KEYS (no args needed)
    if (cmd.name == "TTL") return cmd.args.size() >= 1;   // TTL key
    if (cmd.name == "INFO") return true;                   // INFO
    if (cmd.name == "PING") return true;                   // PING
    if (cmd.name == "QUIT") return true;                   // QUIT

    return false;  // Unknown command
}

} // namespace kvstore
