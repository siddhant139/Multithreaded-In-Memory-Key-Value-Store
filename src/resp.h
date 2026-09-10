#pragma once

#include "types.h"
#include <string>

namespace kvstore {

// Parses RESP (REdis Serialization Protocol) *requests*.
//
// A client request in RESP is always an array of bulk strings, e.g. the command
// `SET foo bar` is sent as:
//
//     *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
//
// where:
//     *3       -> array of 3 elements
//     $3\r\nSET -> a bulk string of length 3 ("SET")
//
// Because TCP is a byte stream, a request may arrive split across multiple
// recv() calls. frameLength() tells the server whether a *complete* command is
// present in the buffer yet, so it knows when it can safely parse.
class Resp {
public:
    // Inspect the buffer and return the byte length of the first complete RESP
    // command, or:
    //    0  -> the command is incomplete; the server should read more bytes
    //   -1  -> a protocol error (malformed input)
    static long frameLength(const std::string& buffer);

    // Parse a buffer that is known to contain a complete command (the first
    // `frameLength()` bytes) into a Command { name, args }.
    static Command parse(const std::string& buffer);

    // Serialize a Command back into RESP wire format (an array of bulk strings).
    // Used to write commands to the append-only file, where the length-prefixed
    // encoding safely round-trips values containing spaces, newlines, or binary.
    static std::string encode(const Command& cmd);
};

} // namespace kvstore
