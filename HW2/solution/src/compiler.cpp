#include "compiler.h"

template<typename T>
T inline ceil_div(T a, T b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    return 1 + (a - 1) / b;
}

std::vector<Block> Compiler::find_basic_blocks() const {
    std::vector<Block> basic_blocks;

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
    auto basic_blocks = find_basic_blocks();
    if (basic_blocks.size() == 1) {
        return 0;
    }

    // loop body is basic block 1
    uint32_t alu_instructions {0};
    uint32_t mult_instructions {0};
    uint32_t mem_instructions {0};
    uint32_t branch_instructions {0};
    for (uint64_t i {basic_blocks.at(1).first}; i < basic_blocks.at(1).second; ++i) {
        switch (m_program.at(i).op) {
        case Opcode::add:
        case Opcode::addi:
        case Opcode::sub:
            ++alu_instructions;
            break;
        case Opcode::mulu:
            ++mult_instructions;
            break;
        case Opcode::ld:
        case Opcode::st:
            ++mem_instructions;
            break;
        case Opcode::loop:
        case Opcode::loop_pip:
                ++branch_instructions;
                break;
        default:
                break;
        }
    }

    uint32_t init_itvl {0};
    init_itvl = std::max(init_itvl, ceil_div(alu_instructions, num_alu));
    init_itvl = std::max(init_itvl, ceil_div(mult_instructions, num_mult));
    init_itvl = std::max(init_itvl, ceil_div(mem_instructions, num_mem));
    init_itvl = std::max(init_itvl, ceil_div(branch_instructions, num_branch));
    return init_itvl;
}

std::vector<Dependency> Compiler::find_dependencies(std::vector<Block> blocks) const {
    // dependency vector for each instruction
    std::vector<Dependency> result(m_program.size());

    // find local dependencies
    for (const Block& block : blocks) {
        // producers map a register to the address of the instruction that
        // writes to the register
        std::vector<int32_t> producers(num_registers_with_special, -1);

        // in this for loop, while we update the producers map, we check if the
        // current instruction is a consumer of any producers
        for (auto i {block.first + 1}; i != block.second; ++i) {
            // update producer
            const auto& instr = m_program.at(i - 1);
            if (instr.op != Opcode::st &&
                instr.op != Opcode::loop &&
                instr.op != Opcode::loop_pip &&
                instr.op != Opcode::nop &&
                instr.op != Opcode::movp) {
                // TODO: do we need to exclude movp?
                assert(instr.dest < num_registers_with_special);
                producers.at(instr.dest) = i - 1;
            }

            // check if the current instruction is a consumer of any producers
            auto op {m_program.at(i).op};
            switch (op) {
            case Opcode::add:
            case Opcode::sub:
            case Opcode::mulu: {
                auto op_a {m_program.at(i).op_a};
                auto op_b {m_program.at(i).op_b};
                if (producers.at(op_a) != -1) {
                    result.at(i).local.push_back(producers.at(op_a));
                }
                if (producers.at(op_b) != -1) {
                    result.at(i).local.push_back(producers.at(op_b));
                }
                break;
            }
            case Opcode::addi:
            case Opcode::ld:
            case Opcode::st:
            case Opcode::movr: {
                auto op_a {m_program.at(i).op_a};
                if (producers.at(op_a) != -1) {
                    result.at(i).local.push_back(producers.at(op_a));
                }
                break;
            }
            default:
                break;
            }
        }
    }

    // TODO: find other dependencies

    return result;
}