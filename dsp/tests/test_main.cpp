// test_main.cpp - runner for the tests registered with TEST_CASE.
#include "test_framework.hpp"

#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    using namespace gigrig::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failed = 0;
    for (const TestCase& t : registry()) {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr) {
            continue;
        }
        ++run;
        currentFailures() = 0;
        try {
            t.fn();
        } catch (const RequireFailed&) {
            // already reported
        } catch (const std::exception& e) {
            reportFailure(t.file, t.line, e.what());
        } catch (...) {
            reportFailure(t.file, t.line, "unknown exception");
        }
        if (currentFailures() > 0) {
            ++failed;
            std::printf("[FAIL] %s\n", t.name);
        } else {
            std::printf("[ OK ] %s\n", t.name);
        }
    }
    std::printf("\n%d test(s) run, %d failed\n", run, failed);
    if (run == 0) {
        std::printf("no tests matched\n");
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
