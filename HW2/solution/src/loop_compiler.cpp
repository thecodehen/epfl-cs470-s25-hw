#include "loop_compiler.h"

#include <iomanip>
#include <iostream>
#include <algorithm>

VLIWProgram LoopCompiler::compile() {
    auto basic_blocks = find_basic_blocks();

    // print basic blocks for debugging
    for (const auto& block : basic_blocks) {
        uint64_t start = block.first;
        uint64_t end = block.second;

        std::cout << "Basic block: " << start << " to " << end << std::endl;
    }

    // compute the minimum initiation interval
    std::cout << "min II = " << compute_min_initiation_interval() << std::endl;

    // find dependencies
    auto dependencies = find_dependencies(basic_blocks);
    
    // Debug dependency information
    for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
        std::cout << std::setfill('0') << std::setw(5)
            << std::distance(dependencies.begin(), it) << ": ";
        std::cout << "local: ";
        for (auto i : it->local) {
            std::cout << i << " ";
        }
        std::cout << std::endl;
    }

    // Schedule instructions (main scheduling algorithm)
    schedule(dependencies);

    // Create VLIWProgram from bundles
    VLIWProgram program;
    
    // For each bundle, create a VLIW instruction with all functional units
    for (const auto& bundle : m_bundles) {
        // For each functional unit slot, use the assigned instruction or nop if empty
        Instruction alu0 = bundle[0] ? *bundle[0] : Instruction{Opcode::nop};
        Instruction alu1 = bundle[1] ? *bundle[1] : Instruction{Opcode::nop};
        Instruction mult = bundle[2] ? *bundle[2] : Instruction{Opcode::nop};
        Instruction mem = bundle[3] ? *bundle[3] : Instruction{Opcode::nop};
        Instruction branch = bundle[4] ? *bundle[4] : Instruction{Opcode::nop};
        
        // Add this cycle's instructions to the final program
        program.alu0_instructions.push_back(alu0);
        program.alu1_instructions.push_back(alu1);
        program.mult_instructions.push_back(mult);
        program.mem_instructions.push_back(mem);
        program.branch_instructions.push_back(branch);
    }
    
    return program;
}

/**
 * Main scheduling function - orchestrates the scheduling of all basic blocks
 */
std::vector<uint64_t> LoopCompiler::schedule(std::vector<Dependency>& dependencies) const {
    // Initialize time table to map instructions to bundles
    std::vector<uint64_t> time_table(m_program.size(), UINT64_MAX);
    
    // Clear any previous scheduling data
    m_bundles.clear();
    
    // Get basic blocks
    auto basic_blocks = find_basic_blocks();
    
    // Schedule each basic block in order
    // First the pre-loop code (BB0)
    schedule_bb0(time_table);
    
    // Then if there's a loop, schedule the loop body (BB1)
    if (basic_blocks.size() > 1) {
        m_time_start_of_loop = m_bundles.size(); // Mark start of loop
        schedule_bb1(time_table);
        m_time_end_of_loop = m_bundles.size(); // Mark end of loop
        
        // Finally, if there's post-loop code, schedule it (BB2)
        if (basic_blocks.size() > 2) {
            schedule_bb2(time_table);
        }
    }
    
    return time_table;
}

/**
 * Attempts to find the earliest possible bundle position for an instruction
 * Respects functional unit constraints (each bundle has limited units)
 */
bool LoopCompiler::insert_ASAP(uint64_t instr_id, uint64_t lowest_time, 
                              std::vector<uint64_t>& time_table) const {
    const auto& instr = m_program[instr_id];
    
    // Make sure we have enough bundles to consider
    while (m_bundles.size() <= lowest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    
    // Try each bundle starting from the lowest possible time
    for (uint64_t i_bundle = lowest_time; i_bundle < m_bundles.size(); ++i_bundle) {
        // Determine instruction type and check appropriate functional unit
        if (instr.op == Opcode::add || instr.op == Opcode::addi || 
            instr.op == Opcode::sub || instr.op == Opcode::movi || 
            instr.op == Opcode::movr || instr.op == Opcode::movp || 
            instr.op == Opcode::nop) {
            // ALU operations - check ALU0 then ALU1
            if (m_bundles[i_bundle][0] == nullptr) {
                // ALU0 is available
                m_bundles[i_bundle][0] = &instr;
                time_table[instr_id] = i_bundle;
                return true;
            } else if (m_bundles[i_bundle][1] == nullptr) {
                // ALU1 is available
                m_bundles[i_bundle][1] = &instr;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::mulu) {
            // Multiplication operation - check MUL unit
            if (m_bundles[i_bundle][2] == nullptr) {
                m_bundles[i_bundle][2] = &instr;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
            // Memory operations - check MEM unit
            if (m_bundles[i_bundle][3] == nullptr) {
                m_bundles[i_bundle][3] = &instr;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
            // Branch operations - check BRANCH unit
            if (m_bundles[i_bundle][4] == nullptr) {
                m_bundles[i_bundle][4] = &instr;
                time_table[instr_id] = i_bundle;
                return true;
            }
        }
    }
    
    // Could not find a suitable slot in existing bundles
    return false;
}

/**
 * Creates a new bundle for an instruction when ASAP insertion fails
 * Called when no existing bundle has an appropriate functional unit available
 */
void LoopCompiler::append(uint64_t instr_id, uint64_t lowest_time,
                        std::vector<uint64_t>& time_table) const {
    const auto& instr = m_program[instr_id];
    
    // Make sure we have enough bundles up to lowest_time
    while (m_bundles.size() <= lowest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    
    // Add a new bundle if needed
    if (m_bundles.size() <= lowest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    
    // Place instruction in the appropriate slot based on its type
    uint64_t bundle_idx = std::max(m_bundles.size() - 1, lowest_time);
    
    // If we need to create a new bundle at a specific position
    while (m_bundles.size() <= bundle_idx) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    
    // Determine which functional unit to use based on instruction type
    if (instr.op == Opcode::add || instr.op == Opcode::addi || 
        instr.op == Opcode::sub || instr.op == Opcode::movi || 
        instr.op == Opcode::movr || instr.op == Opcode::movp || 
        instr.op == Opcode::nop) {
        m_bundles[bundle_idx][0] = &instr;  // ALU0
    } else if (instr.op == Opcode::mulu) {
        m_bundles[bundle_idx][2] = &instr;  // MUL
    } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
        m_bundles[bundle_idx][3] = &instr;  // MEM
    } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
        m_bundles[bundle_idx][4] = &instr;  // BRANCH
    }
    
    // Update time table with the bundle assignment
    time_table[instr_id] = bundle_idx;
}

/**
 * Schedule basic block 0 (pre-loop instructions)
 */
std::vector<uint64_t> LoopCompiler::schedule_bb0(std::vector<uint64_t>& time_table) const {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // Process each instruction in BB0
    for (uint64_t i = basic_blocks[0].first; i < basic_blocks[0].second; ++i) {
        uint64_t lowest_time = 0;
        
        // Calculate the earliest possible time based on dependencies
        for (uint64_t dep_id : dependencies[i].local) {
            // Each dependent instruction has a latency
            // MUL operations have 3 cycle latency, others have 1
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Try to insert ASAP, if not possible, append a new bundle
        if (!insert_ASAP(i, lowest_time, time_table)) {
            append(i, lowest_time, time_table);
        }
    }
    
    return time_table;
}

/**
 * Schedule basic block 1 (loop body instructions)
 */
std::vector<uint64_t> LoopCompiler::schedule_bb1(std::vector<uint64_t>& time_table) const {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // If BB1 is empty, just set markers and return
    if (basic_blocks[1].first >= basic_blocks[1].second) {
        m_time_start_of_loop = m_bundles.size();
        m_time_end_of_loop = m_bundles.size();
        return time_table;
    }
    
    // Calculate lowest possible time to start the loop based on dependencies
    uint64_t lowest_time_start_loop = m_bundles.size();
    
    // Process loop instructions except the last one (loop or loop.pip)
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
        // Check loop invariant dependencies (dependencies from outside the loop)
        for (uint64_t dep_id : dependencies[i].loop_invariant) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time_start_loop = std::max(lowest_time_start_loop, 
                                          time_table[dep_id] + latency);
        }
        
        // Check interloop dependencies from BB0
        for (uint64_t dep_id : dependencies[i].interloop) {
            // Only consider dependencies from BB0 for now
            if (dep_id < basic_blocks[1].first) {
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                lowest_time_start_loop = std::max(lowest_time_start_loop, 
                                              time_table[dep_id] + latency);
            }
        }
    }
    
    // Schedule loop body instructions (except last loop instruction)
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
        uint64_t lowest_time = lowest_time_start_loop;
        
        // Check local dependencies within the loop
        for (uint64_t dep_id : dependencies[i].local) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Try to insert ASAP, if not possible, append a new bundle
        if (!insert_ASAP(i, lowest_time, time_table)) {
            append(i, lowest_time, time_table);
        }
    }
    
    // Handle the last instruction (loop or loop.pip)
    uint64_t loop_ins_idx = basic_blocks[1].second - 1;
    
    // Store loop start address in the instruction
    Instruction& loop_instr = const_cast<Instruction&>(m_program[loop_ins_idx]);
    loop_instr.imm = lowest_time_start_loop;
    
    // Calculate time needed after loop to satisfy all dependencies
    uint64_t time_need_after_loop = 0;
    
    // Check for any interloop dependencies that need to be considered
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
        for (uint64_t dep_id : dependencies[i].interloop) {
            // Only consider loop body dependencies
            if (dep_id >= basic_blocks[1].first && dep_id < basic_blocks[1].second) {
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                
                // Time calculation
                uint64_t tmp_time_afterward = (m_bundles.size() - time_table[dep_id]);
                uint64_t tmp_time_before = (time_table[i] - lowest_time_start_loop);
                int64_t tmp_time_need = latency - tmp_time_afterward - tmp_time_before;
                
                time_need_after_loop = std::max(time_need_after_loop, 
                                           tmp_time_need > 0 ? (uint64_t)tmp_time_need : 0);
            }
        }
    }
    
    // Schedule the loop instruction
    uint64_t loop_time = m_bundles.size() + time_need_after_loop;
    if (!insert_ASAP(loop_ins_idx, loop_time, time_table)) {
        append(loop_ins_idx, loop_time, time_table);
    }
    
    // Set loop end time
    m_time_end_of_loop = m_bundles.size();
    
    return time_table;
}

/**
 * Schedule basic block 2 (post-loop instructions)
 */
std::vector<uint64_t> LoopCompiler::schedule_bb2(std::vector<uint64_t>& time_table) const {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // Process each instruction in BB2
    for (uint64_t i = basic_blocks[2].first; i < basic_blocks[2].second; ++i) {
        // Start from the current end of schedule
        uint64_t lowest_time = m_bundles.size();
        
        // Check loop invariant dependencies
        for (uint64_t dep_id : dependencies[i].loop_invariant) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Check post-loop dependencies
        for (uint64_t dep_id : dependencies[i].post_loop) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Check local dependencies
        for (uint64_t dep_id : dependencies[i].local) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Try to insert ASAP, if not possible, append a new bundle
        if (!insert_ASAP(i, lowest_time, time_table)) {
            append(i, lowest_time, time_table);
        }
    }
    
    return time_table;
}
