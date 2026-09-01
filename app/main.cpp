#include <cstring>
#include <iostream>

extern "C" {
#include "sym_types.h"
}

namespace {
constexpr const char* kVersion = "0.0.0-phase0";
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "symbolon " << kVersion << "\n";
            return 0;
        }
    }

    std::cout << "symbolon " << kVersion
              << " -- Phase 0 skeleton, no signal chain wired up yet.\n";
    return 0;
}
