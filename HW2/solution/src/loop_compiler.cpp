#include "loop_compiler.h"

#include <iomanip>
#include <iostream>

VLIWProgram LoopCompiler::compile() {
    auto basic_blocks = find_basic_blocks();

    // print basic blocks
    for (const auto& block : basic_blocks) {
        uint64_t start = block.first;
        uint64_t end = block.second;

        std::cout << "Basic block: " << start << " to " << end << std::endl;
    }

    // compute the minimum initiation interval
    std::cout << "min II = " << compute_min_initiation_interval() << std::endl;

    // find dependencies
    auto dependencies = find_dependencies(basic_blocks);
    for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
        std::cout << std::setfill('0') << std::setw(5)
            << std::distance(dependencies.begin(), it) << ": ";
        std::cout << "local: ";
        for (auto i : it->local) {
            std::cout << i << " ";
        }
        std::cout << std::endl;
    }

    return VLIWProgram{};
}
