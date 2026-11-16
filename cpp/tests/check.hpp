#pragma once

// The whole test framework. No external dependency: a transparency format
// whose reference implementation needs a package manager to test gets
// tested by fewer of the people it is meant to be checkable by.

#include <cstdio>
#include <string>

namespace check {

inline int& checks() { static int n = 0; return n; }
inline int& failures() { static int n = 0; return n; }
inline const char*& current() { static const char* name = ""; return name; }

inline void record(bool ok, const char* expression, const char* file, int line) {
    ++checks();
    if (!ok) {
        ++failures();
        std::printf("  FAIL  %s\n        %s:%d  %s\n", current(), file, line, expression);
    }
}

struct Scope {
    explicit Scope(const char* name) {
        current() = name;
        std::printf("  %s\n", name);
    }
};

}  // namespace check

#define CHECK(expression) check::record((expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::record((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define TEST(name) check::Scope test_scope_(name)

void runJsonTests();
void runTimeTests();
void runOtb1Tests();
