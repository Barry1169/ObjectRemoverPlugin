// SPDX-License-Identifier: MIT
#include "TestCase.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    // Optional substring filter, so a single suite can be run on its own:
    //   ./aibgr_tests premultiply
    const std::string filter = (argc > 1) ? argv[1] : std::string();

    std::printf("aibgr_core unit tests\n");
    std::printf("---------------------\n");

    const int failures = aibgr::test::Registry::instance().runAll(filter.c_str());
    return failures == 0 ? 0 : 1;
}
