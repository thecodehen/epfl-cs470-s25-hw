#include "compiler.h"

#include <iostream>

template<typename T>
T ceil_div(T a, T b) {
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

std::vector<std::pair<uint32_t, uint32_t>> Compiler::find_instr_dependency(
    const std::array<int32_t, num_registers_with_special>& producers,
    const Instruction& instr
) const {
    std::vector<std::pair<uint32_t, uint32_t>> results;
    auto op {instr.op};

    switch (op) {
    case Opcode::add:
    case Opcode::sub:
    case Opcode::mulu: {
        auto op_a {instr.op_a};
        auto op_b {instr.op_b};
        if (producers.at(op_a) >= 0) {
            results.push_back({producers.at(op_a), op_a});
        }
        if (producers.at(op_b) >= 0) {
            results.push_back({producers.at(op_b), op_b});
        }
        break;
    }
    case Opcode::addi:
    case Opcode::ld:
    case Opcode::movr: {
        auto op_a {instr.op_a};
        if (producers.at(op_a) >= 0) {
            results.push_back({producers.at(op_a), op_a});
        }
        break;
    }
    case Opcode::st: {
        auto dest {instr.dest};
        auto op_a {instr.op_a};
        if (producers.at(dest) >= 0) {
            results.push_back({producers.at(dest), dest});
        }
        if (producers.at(op_a) >= 0) {
            results.push_back({producers.at(op_a), op_a});
        }
        break;
    }
    default:
        break;
    }
    return results;
}

bool Compiler::is_producer(const Opcode opcode)
{
    if (opcode != Opcode::st &&
        opcode != Opcode::loop &&
        opcode != Opcode::loop_pip &&
        opcode != Opcode::nop &&
        opcode != Opcode::movp) {
        // TODO: do we need to exclude movp?
        return true;
    }
    return false;
}

void Compiler::update_producers(
    std::array<int32_t, num_registers_with_special>& producers,
    const int32_t instr_idx
) const {
    if (const auto& instr = m_program.at(instr_idx); is_producer(instr.op)) {
        assert(instr.dest < num_registers_with_special);
        producers.at(instr.dest) = instr_idx;
    }
}

void Compiler::remove_duplicate_dependencies(std::vector<Dependency>& dependencies)
{
    // remove duplicate dependencies
    for (auto& dep : dependencies) {
        std::sort(dep.local.begin(), dep.local.end());
        dep.local.erase(std::unique(dep.local.begin(), dep.local.end()), dep.local.end());

        std::sort(dep.interloop.begin(), dep.interloop.end());
        dep.interloop.erase(std::unique(dep.interloop.begin(), dep.interloop.end()), dep.interloop.end());

        std::sort(dep.loop_invariant.begin(), dep.loop_invariant.end());
        dep.loop_invariant.erase(std::unique(dep.loop_invariant.begin(), dep.loop_invariant.end()), dep.loop_invariant.end());

        std::sort(dep.post_loop.begin(), dep.post_loop.end());
        dep.post_loop.erase(std::unique(dep.post_loop.begin(), dep.post_loop.end()), dep.post_loop.end());
    }
}

std::vector<Dependency> Compiler::find_dependencies(std::vector<Block> blocks) const {
    // dependency vector for each instruction
    std::vector<Dependency> result(m_program.size());

    // producers map a register to the address of the instruction that
    // writes to the register
    std::array<int32_t, num_registers_with_special> producers;

    // find local dependencies
    for (const Block& block : blocks) {
        std::fill(producers.begin(), producers.end(), -1);

        // in this for loop, while we update the producers map, we check if the
        // current instruction is a consumer of any producers
        for (auto i {block.first + 1}; i < block.second; ++i) {
            // update producer
            update_producers(producers, i - 1);

            const auto deps = find_instr_dependency(
                producers,
                m_program.at(i)
            );
            // add the dependencies to the instruction
            std::transform(deps.begin(), deps.end(), std::back_inserter(result.at(i).local),
                [](const auto& dep) {
                    return dep.first;
                }
            );
        }
    }

    // if there is only one basic block, there are no other dependencies
    if (blocks.size() == 1) {
        remove_duplicate_dependencies(result);
        return result;
    }

    // else there are 3 basic blocks
    const Block bb0 = blocks.at(0), bb1 = blocks.at(1), bb2 = blocks.at(2);

    // get all bb0 and bb1 producers
    // the instruction that bb0_producers map to is the last instruction that produces the register in bb0
    std::array<int32_t, num_registers_with_special> bb0_producers;
    std::array<int32_t, num_registers_with_special> bb1_producers;
    std::fill(bb0_producers.begin(), bb0_producers.end(), -1);
    std::fill(bb1_producers.begin(), bb1_producers.end(), -1);
    for (auto i {bb0.first}; i != bb0.second; ++i) {
        update_producers(bb0_producers, i);
    }
    for (auto i {bb1.first}; i != bb1.second; ++i) {
        update_producers(bb1_producers, i);
    }

    // find interloop dependencies
    // dependencies from a previous loop iteration to the next loop iteration
    // because of that, all interloop dependencies must be produced after the instruction (and also initially produced
    // in bb0)
    std::fill(producers.begin(), producers.end(), -1);
    for (auto i {bb1.second - 1}; i >= bb1.first; --i) {
        update_producers(producers, i);
        const auto& instr = m_program.at(i);
        const auto deps = find_instr_dependency(producers, instr);
        // add the dependencies to the instruction
        std::transform(deps.begin(), deps.end(), std::back_inserter(result.at(i).interloop),
            [](const auto& dep) {
                return dep.first;
            }
        );

        // check if the same register is produced in bb0
        for (const auto [instr_id, reg_id] : deps) {
            if (bb0_producers.at(reg_id) != -1) {
                result.at(i).interloop.push_back(bb0_producers.at(reg_id));
            }
        }
    }

    // loop invariant dependencies
    // these dependencies are produced from bb0, but consumed in bb1 or bb2. They cannot be produced anywhere else
    // before they are consumed
    for (auto i {bb1.first}; i != bb1.second; ++i) {
        const auto& instr = m_program.at(i);
        const auto deps = find_instr_dependency(bb0_producers, instr);
        std::for_each(deps.begin(), deps.end(), [&](auto d) {
            auto [instr_id, reg_id] = d;
            // check if the dependency is not produced in bb1 (the loop body)
            if (std::find(
                    result.at(i).interloop.begin(),
                    result.at(i).interloop.end(),
                    instr_id
                    ) != result.at(i).interloop.end()) {
                return;
            }
            result.at(i).loop_invariant.push_back(instr_id);
        });
    }
    for (auto i {bb2.first}; i != bb2.second; ++i) {
        const auto& instr = m_program.at(i);
        const auto deps = find_instr_dependency(bb0_producers, instr);
        std::for_each(deps.begin(), deps.end(), [&](auto d) {
            auto [instr_id, reg_id] = d;
            // check if the dependency is not produced in bb1 (the loop body)
            if (!find_instr_dependency(bb1_producers, instr).empty()) {
                return;
            }
            // check if the dependency is not produced in bb2 (locally)
            if (std::find(result.at(i).local.begin(), result.at(i).local.end(), instr_id) != result.at(i).local.end()) {
                return;
            }
            result.at(i).loop_invariant.push_back(instr_id);
        });
    }

    // post loop dependencies
    for (auto i {bb2.first}; i != bb2.second; ++i) {
        const auto& instr = m_program.at(i);
        const auto deps = find_instr_dependency(bb1_producers, instr);
        std::transform(deps.begin(), deps.end(), std::back_inserter(result.at(i).post_loop),
            [](const auto& dep) {
                return dep.first;
            }
        );
    }

    remove_duplicate_dependencies(result);
    return result;
}