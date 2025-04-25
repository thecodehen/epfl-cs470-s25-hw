#include "loop_compiler.h"

#include <iostream>

VLIWProgram LoopCompiler::compile() {
    auto basic_blocks = find_basic_blocks();

    // print basic blocks
    for (const auto& block : basic_blocks) {
        uint64_t start = block.first;
        uint64_t end = block.second;

        std::cout << "Basic block: " << start << " to " << end << std::endl;
    }

	return VLIWProgram{};
}
