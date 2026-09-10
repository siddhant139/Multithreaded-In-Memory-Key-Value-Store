#pragma once

#include "types.h"
#include <string>

namespace kvstore {

class Parser {
public:
    // Parse a raw input line into a Command struct
    // Input format: COMMAND arg1 arg2 ...
    // Examples:
    //   SET key value        -> {name: "SET", args: ["key", "value"]}
    //   SET key value EX 60  -> {name: "SET", args: ["key", "value", "EX", "60"]}
    //   GET key              -> {name: "GET", args: ["key"]}
    //   DEL key1 key2        -> {name: "DEL", args: ["key1", "key2"]}
    static Command parse(const std::string& raw_input);

    // Validate command has minimum required args
    static bool validate(const Command& cmd);
};

} // namespace kvstore
