#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

namespace kvstore {

// A protocol-agnostic reply produced by the command handler.
//
// The handler builds a structured Reply; the server then serializes it into
// whichever wire format the client is speaking:
//   - toResp()   -> RESP (REdis Serialization Protocol), compatible with redis-cli
//   - toInline() -> human-readable text, for interactive use via nc / telnet
//
// This decouples the command logic from the transport encoding, so the same
// handler serves both protocols with no branching in the business logic.
struct Reply {
    enum class Type {
        SIMPLE_STRING,  // e.g. OK, PONG      -> RESP "+...\r\n"
        ERROR,          // error message      -> RESP "-ERR ...\r\n"
        INTEGER,        // e.g. 1, 0, -2       -> RESP ":...\r\n"
        BULK_STRING,    // arbitrary value     -> RESP "$<len>\r\n...\r\n"
        NIL,            // missing value       -> RESP "$-1\r\n"
        ARRAY           // list of replies     -> RESP "*<count>\r\n..."
    };

    Type type = Type::SIMPLE_STRING;
    std::string str;             // used by SIMPLE_STRING / ERROR / BULK_STRING
    int64_t integer = 0;         // used by INTEGER
    std::vector<Reply> elements; // used by ARRAY

    // ---- Factory helpers (make call sites read cleanly) ----
    static Reply simple(const std::string& s) { Reply r; r.type = Type::SIMPLE_STRING; r.str = s; return r; }
    static Reply error(const std::string& s)  { Reply r; r.type = Type::ERROR;         r.str = s; return r; }
    static Reply integ(int64_t n)             { Reply r; r.type = Type::INTEGER;        r.integer = n; return r; }
    static Reply bulk(const std::string& s)   { Reply r; r.type = Type::BULK_STRING;    r.str = s; return r; }
    static Reply nil()                        { Reply r; r.type = Type::NIL;            return r; }
    static Reply array(std::vector<Reply> e)  { Reply r; r.type = Type::ARRAY;          r.elements = std::move(e); return r; }

    // ---- RESP wire format (redis-cli compatible) ----
    std::string toResp() const {
        switch (type) {
            case Type::SIMPLE_STRING:
                return "+" + str + "\r\n";
            case Type::ERROR:
                return "-ERR " + str + "\r\n";
            case Type::INTEGER:
                return ":" + std::to_string(integer) + "\r\n";
            case Type::BULK_STRING:
                return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
            case Type::NIL:
                return "$-1\r\n";
            case Type::ARRAY: {
                std::string out = "*" + std::to_string(elements.size()) + "\r\n";
                for (const auto& e : elements) out += e.toResp();
                return out;
            }
        }
        return "";
    }

    // ---- Human-readable inline format (nc / telnet); matches the original text protocol ----
    std::string toInline() const {
        switch (type) {
            case Type::SIMPLE_STRING:
                return str;
            case Type::ERROR:
                return "ERROR: " + str;
            case Type::INTEGER:
                return "(integer) " + std::to_string(integer);
            case Type::BULK_STRING:
                return str;
            case Type::NIL:
                return "(nil)";
            case Type::ARRAY: {
                if (elements.empty()) return "(empty list)";
                std::ostringstream oss;
                for (size_t i = 0; i < elements.size(); ++i) {
                    oss << (i + 1) << ") " << elements[i].toInline();
                    if (i + 1 < elements.size()) oss << "\n";
                }
                return oss.str();
            }
        }
        return "";
    }
};

} // namespace kvstore
