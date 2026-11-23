// test_framework.hpp - a minimal, dependency-free test harness for GigRigDSP.
//
// Usage:
//   TEST_CASE("passthrough keeps samples") { CHECK(x == y); REQUIRE(p != nullptr); }
//
// CHECK records a failure and continues; REQUIRE records a failure and ends
// the current test. CHECK_NEAR compares floating-point values with a tolerance.
// The runner (test_main.cpp) runs every registered test, or only those whose
// name contains the first command-line argument.
#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

namespace gigrig::test {

using TestFn = void (*)();

struct TestCase {
    const char* name;
    const char* file;
    int line;
    TestFn fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, const char* file, int line, TestFn fn) {
        registry().push_back({name, file, line, fn});
    }
};

struct RequireFailed {};

inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

inline void reportFailure(const char* file, int line, const char* expr) {
    ++currentFailures();
    std::fprintf(stderr, "  %s:%d: FAILED: %s\n", file, line, expr);
}

}  // namespace gigrig::test

#define GIGRIG_TEST_CONCAT2(a, b) a##b
#define GIGRIG_TEST_CONCAT(a, b) GIGRIG_TEST_CONCAT2(a, b)

#define TEST_CASE_IMPL(name, fn)                                                        \
    static void fn();                                                                   \
    static const ::gigrig::test::Registrar GIGRIG_TEST_CONCAT(fn, _registrar){name,     \
                                                                             __FILE__,  \
                                                                             __LINE__,  \
                                                                             &fn};      \
    static void fn()

#define TEST_CASE(name) TEST_CASE_IMPL(name, GIGRIG_TEST_CONCAT(gigrig_test_, __LINE__))

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) ::gigrig::test::reportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

#define REQUIRE(expr)                                                       \
    do {                                                                    \
        if (!(expr)) {                                                      \
            ::gigrig::test::reportFailure(__FILE__, __LINE__, #expr);       \
            throw ::gigrig::test::RequireFailed{};                          \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                          \
    do {                                                                                 \
        const double gigrig_a_ = static_cast<double>(actual);                            \
        const double gigrig_e_ = static_cast<double>(expected);                          \
        if (!(std::fabs(gigrig_a_ - gigrig_e_) <= static_cast<double>(tolerance))) {     \
            ::gigrig::test::reportFailure(__FILE__, __LINE__,                            \
                                          #actual " near " #expected);                   \
            std::fprintf(stderr, "    actual %.9g, expected %.9g, tolerance %.3g\n",     \
                         gigrig_a_, gigrig_e_, static_cast<double>(tolerance));          \
        }                                                                                \
    } while (0)
