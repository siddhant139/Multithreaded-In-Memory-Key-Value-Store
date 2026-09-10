#include "resp.h"

#include <algorithm>
#include <cctype>

namespace kvstore {

// Returns the total byte length of the first complete RESP command in `buf`,
// 0 if more bytes are needed, or -1 on a protocol error.
//
// The scan mirrors the wire format exactly:
//   *<count>\r\n                       (array header)
//   $<len>\r\n<len bytes>\r\n           (one bulk string, repeated <count> times)
long Resp::frameLength(const std::string& buf) {
    if (buf.empty()) return 0;
    if (buf[0] != '*') return -1;  // a client command must be a RESP array

    // --- Parse the array element count: *<count>\r\n ---
    size_t crlf = buf.find("\r\n", 1);
    if (crlf == std::string::npos) return 0;  // header line not fully received

    long count = 0;
    try {
        count = std::stol(buf.substr(1, crlf - 1));
    } catch (...) {
        return -1;  // non-numeric count
    }
    if (count < 0) return -1;

    size_t pos = crlf + 2;  // advance past "\r\n"

    // --- Parse each bulk string: $<len>\r\n<data>\r\n ---
    for (long i = 0; i < count; ++i) {
        if (pos >= buf.size()) return 0;        // need more bytes
        if (buf[pos] != '$') return -1;         // each element must be a bulk string

        size_t hdr = buf.find("\r\n", pos + 1);
        if (hdr == std::string::npos) return 0; // length line not fully received

        long len = 0;
        try {
            len = std::stol(buf.substr(pos + 1, hdr - (pos + 1)));
        } catch (...) {
            return -1;  // non-numeric length
        }
        if (len < 0) return -1;

        size_t data_start = hdr + 2;
        size_t data_end = data_start + static_cast<size_t>(len);

        // Need the data plus its trailing "\r\n" to be present.
        if (data_end + 2 > buf.size()) return 0;

        pos = data_end + 2;  // advance past data + "\r\n"
    }

    return static_cast<long>(pos);
}

// Parse a buffer whose first bytes form a complete RESP command.
// (Assumes frameLength(buf) > 0 was already checked by the caller.)
Command Resp::parse(const std::string& buf) {
    Command cmd;

    size_t crlf = buf.find("\r\n", 1);
    long count = std::stol(buf.substr(1, crlf - 1));
    size_t pos = crlf + 2;

    std::vector<std::string> parts;
    parts.reserve(static_cast<size_t>(count));

    for (long i = 0; i < count; ++i) {
        size_t hdr = buf.find("\r\n", pos + 1);
        long len = std::stol(buf.substr(pos + 1, hdr - (pos + 1)));
        size_t data_start = hdr + 2;
        parts.push_back(buf.substr(data_start, static_cast<size_t>(len)));
        pos = data_start + static_cast<size_t>(len) + 2;
    }

    if (!parts.empty()) {
        // Command name is case-insensitive; uppercase it to match the handler.
        std::string name = parts[0];
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        cmd.name = name;
        for (size_t i = 1; i < parts.size(); ++i) {
            cmd.args.push_back(parts[i]);
        }
    }

    return cmd;
}

std::string Resp::encode(const Command& cmd) {
    // *<count>\r\n  then a bulk string ($<len>\r\n<data>\r\n) for name + each arg.
    const size_t count = 1 + cmd.args.size();
    std::string out = "*" + std::to_string(count) + "\r\n";
    out += "$" + std::to_string(cmd.name.size()) + "\r\n" + cmd.name + "\r\n";
    for (const auto& arg : cmd.args) {
        out += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
    }
    return out;
}

} // namespace kvstore
