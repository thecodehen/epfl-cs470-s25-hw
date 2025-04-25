#include "compiler.h"

std::vector<std::pair<uint64_t, uint64_t>> Compiler::find_basic_blocks() const {
    std::vector<std::pair<uint64_t, uint64_t>> basic_blocks;

    // Find the first loop or loop.pip instruction
    const auto it = std::find_if(m_program.begin(), m_program.end(),
        [](const Instruction& instr) {
            return instr.op == Opcode::loop || instr.op == Opcode::loop_pip;
        }
    );

    if (it == m_program.end()) {
        // no loop instruction found
        basic_blocks.push_back({0, m_program.size()});
    } else {
        uint64_t loop_start = static_cast<uint64_t>(it->imm);
        uint64_t loop_end = static_cast<uint64_t>(std::distance(m_program.begin(), it));
        basic_blocks.push_back({0, loop_start});
        basic_blocks.push_back({loop_start, loop_end + 1});
        basic_blocks.push_back({loop_end + 1, m_program.size()});
    }

    return basic_blocks;
}

uint32_t Compiler::compute_min_initiation_interval() const {
    return 0;
}