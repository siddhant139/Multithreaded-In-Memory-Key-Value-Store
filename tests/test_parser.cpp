// Standalone test for kvstore::Parser
// Compile: g++ -std=c++17 -I../src test_parser.cpp ../src/parser.cpp -o test_parser

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "../src/parser.h"

using kvstore::Parser;
using kvstore::Command;

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

// ---------------------------------------------------------------------------
// parse() tests
// ---------------------------------------------------------------------------

void test_parse_set_basic() {
    auto cmd = Parser::parse("SET key value");
    check(cmd.name == "SET", "SET basic - command name is SET");
    check(cmd.args.size() == 2, "SET basic - args size is 2");
    check(cmd.args[0] == "key", "SET basic - first arg is 'key'");
    check(cmd.args[1] == "value", "SET basic - second arg is 'value'");
}

void test_parse_get() {
    auto cmd = Parser::parse("GET mykey");
    check(cmd.name == "GET", "GET - command name is GET");
    check(cmd.args.size() == 1, "GET - args size is 1");
    check(cmd.args[0] == "mykey", "GET - first arg is 'mykey'");
}

void test_parse_del_multiple() {
    auto cmd = Parser::parse("DEL k1 k2 k3");
    check(cmd.name == "DEL", "DEL multiple - command name is DEL");
    check(cmd.args.size() == 3, "DEL multiple - args size is 3");
    check(cmd.args[0] == "k1", "DEL multiple - first arg is 'k1'");
    check(cmd.args[1] == "k2", "DEL multiple - second arg is 'k2'");
    check(cmd.args[2] == "k3", "DEL multiple - third arg is 'k3'");
}

void test_parse_set_with_ttl() {
    auto cmd = Parser::parse("SET key val EX 60");
    check(cmd.name == "SET", "SET with TTL - command name is SET");
    check(cmd.args.size() == 4, "SET with TTL - args size is 4");
    check(cmd.args[0] == "key", "SET with TTL - first arg is 'key'");
    check(cmd.args[1] == "val", "SET with TTL - second arg is 'val'");
    check(cmd.args[2] == "EX", "SET with TTL - third arg is 'EX'");
    check(cmd.args[3] == "60", "SET with TTL - fourth arg is '60'");
}

void test_parse_case_insensitivity() {
    auto cmd = Parser::parse("get mykey");
    check(cmd.name == "GET", "Case insensitivity - 'get' becomes 'GET'");
    check(cmd.args.size() == 1, "Case insensitivity - args size is 1");
    check(cmd.args[0] == "mykey", "Case insensitivity - arg preserved as 'mykey'");
}

void test_parse_empty_input() {
    auto cmd = Parser::parse("");
    check(cmd.name.empty(), "Empty input - name is empty");
    check(cmd.args.empty(), "Empty input - args is empty");
}

void test_parse_extra_whitespace() {
    auto cmd = Parser::parse("  SET   key   value  ");
    check(cmd.name == "SET", "Extra whitespace - command name is SET");
    check(cmd.args.size() == 2, "Extra whitespace - args size is 2");
    check(cmd.args[0] == "key", "Extra whitespace - first arg is 'key'");
    check(cmd.args[1] == "value", "Extra whitespace - second arg is 'value'");
}

void test_parse_ping() {
    auto cmd = Parser::parse("PING");
    check(cmd.name == "PING", "PING - command name is PING");
    check(cmd.args.empty(), "PING - args is empty");
}

// ---------------------------------------------------------------------------
// validate() tests
// ---------------------------------------------------------------------------

void test_validate_set_no_args() {
    Command cmd;
    cmd.name = "SET";
    // 0 args -> should fail (needs at least key + value)
    check(!Parser::validate(cmd), "Validate SET with 0 args - fails");
}

void test_validate_set_two_args() {
    Command cmd;
    cmd.name = "SET";
    cmd.args = {"key", "value"};
    check(Parser::validate(cmd), "Validate SET with 2 args - passes");
}

void test_validate_get_no_args() {
    Command cmd;
    cmd.name = "GET";
    // 0 args -> should fail (needs at least 1 key)
    check(!Parser::validate(cmd), "Validate GET with 0 args - fails");
}

void test_validate_keys_no_args() {
    Command cmd;
    cmd.name = "KEYS";
    // KEYS takes no required args
    check(Parser::validate(cmd), "Validate KEYS with 0 args - passes");
}

void test_validate_unknown_command() {
    Command cmd;
    cmd.name = "FOOBAR";
    check(!Parser::validate(cmd), "Validate unknown command 'FOOBAR' - fails");
}

void test_validate_empty_name() {
    Command cmd;
    // name is empty
    check(!Parser::validate(cmd), "Validate empty command name - fails");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== Parser Tests ===" << std::endl;

    std::cout << "\n-- parse() tests --" << std::endl;
    test_parse_set_basic();
    test_parse_get();
    test_parse_del_multiple();
    test_parse_set_with_ttl();
    test_parse_case_insensitivity();
    test_parse_empty_input();
    test_parse_extra_whitespace();
    test_parse_ping();

    std::cout << "\n-- validate() tests --" << std::endl;
    test_validate_set_no_args();
    test_validate_set_two_args();
    test_validate_get_no_args();
    test_validate_keys_no_args();
    test_validate_unknown_command();
    test_validate_empty_name();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===" << std::endl;

    return (tests_failed > 0) ? 1 : 0;
}
