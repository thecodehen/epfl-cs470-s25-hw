#include "loop_pip_compiler.h"

#include <iostream>
#include <algorithm>

/**
 * Main compilation method that implements software pipelining
 * Orchestrates the entire pipeline scheduling process
 */
VLIWProgram LoopPipCompiler::compile() {
    const auto basic_blocks = find_basic_blocks();
    
    // Compute the minimum initiation interval (II)
    m_initiation_interval = compute_min_initiation_interval();
    if (m_initiation_interval == 0) m_initiation_interval = 1;

    
    // Find dependencies between instructions
    const auto dependencies = find_dependencies(basic_blocks);

    // Schedule instructions with pipeline support
    const auto time_table = schedule(basic_blocks, dependencies);
    
    // Organize the pipeline stages
    organize_pipeline_stages();
    
    // Assign predicate registers to instructions based on stage
    assign_predicate_registers();
    
    // Add initialization instructions for predicates and EC
    setup_pipeline_initialization();

    // rename registers
    rename(time_table, dependencies);

    // Create the final VLIW program
    VLIWProgram program;
    
    // For each bundle, create a VLIW instruction with all functional units
    for (const auto& bundle : m_bundles) {
        // For each functional unit slot, use the assigned instruction or nop if empty
        Instruction alu0 = bundle[0] ? *bundle[0] : Instruction{Opcode::nop};
        Instruction alu1 = bundle[1] ? *bundle[1] : Instruction{Opcode::nop};
        Instruction mult = bundle[2] ? *bundle[2] : Instruction{Opcode::nop};
        Instruction mem = bundle[3] ? *bundle[3] : Instruction{Opcode::nop};
        Instruction branch = bundle[4] ? *bundle[4] : Instruction{Opcode::nop};
        
        // Add instructions to the final program
        program.alu0_instructions.push_back(alu0);
        program.alu1_instructions.push_back(alu1);
        program.mult_instructions.push_back(mult);
        program.mem_instructions.push_back(mem);
        program.branch_instructions.push_back(branch);
    }
    
    return program;
}

/**
 * Main scheduling function for pipelined execution
 * Implements modulo scheduling with resource reservation
 */
std::vector<uint64_t> LoopPipCompiler::schedule(
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies
    ) {
    // Initialize time table to map instructions to bundles
    std::vector time_table(m_program.size(), UINT64_MAX);
    
    // Clear any previous scheduling data
    m_bundles.clear();
    m_slot_status.clear();
    
    // Schedule each basic block in order
    // First the pre-loop code (BB0) - identical to non-pipelined version
    schedule_preloop_block(time_table, basic_blocks, dependencies);
    
    // Then schedule the loop body (BB1) with pipelining
    if (basic_blocks.size() > 1) {
        m_loop_start_time = m_bundles.size(); // Mark start of loop
        schedule_loop_body_pipelined(time_table);
        m_loop_end_time = m_bundles.size(); // Mark end of loop
        
        // Finally, schedule post-loop code (BB2)
        if (basic_blocks.size() > 2) {
            schedule_postloop_block(time_table);
        }
    }
    
    return time_table;
}

/**
 * Schedules the first basic block (pre-loop code)
 * Identical to the non-pipelined version
 */
void LoopPipCompiler::schedule_preloop_block(
    std::vector<uint64_t>& time_table,
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies
    ) {
    // Process each instruction in BB0
    assert(basic_blocks.size() > 0);
    const Block bb0 = basic_blocks.at(0);
    for (auto i {bb0.first}; i < bb0.second; ++i) {
        uint64_t lowest_time = 0;
        
        // Calculate the earliest possible time based on dependencies
        for (const uint64_t dep_id : dependencies.at(i).local) {
            // Each dependent instruction has a latency
            // MUL operations have 3 cycle latency, others have 1
            const auto latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table.at(dep_id) + latency);
        }

        schedule_asap(time_table, i, lowest_time);
    }
}

/**
 * Helper function for pre-loop code (identical to LoopCompiler::insert_ASAP)
 * This is used for BB0 where we don't need modulo scheduling yet
 */
void LoopPipCompiler::schedule_asap(
    std::vector<uint64_t>& time_table,
    const uint64_t instr_id,
    const uint64_t lowest_time
    ) {
    const auto& instr = m_program.at(instr_id);
    
    // Make sure we have enough bundles to consider
    while (m_bundles.size() <= lowest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
        m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
    }
    
    // Try each bundle starting from the lowest possible time
    for (auto i_bundle {lowest_time}; i_bundle < m_bundles.size(); ++i_bundle) {
        if (try_schedule(time_table, instr_id, i_bundle)) {
            return;
        }
    }

    // no suitable slot found in existing bundle, need to create a new bundle
    m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
    try_schedule(time_table, instr_id, m_bundles.size() - 1);
}

bool LoopPipCompiler::try_schedule(
    std::vector<uint64_t>& time_table,
    const uint64_t instr_id,
    const uint64_t time
    ) {
    // Determine instruction type and check appropriate functional unit
    const auto& instr = m_program.at(instr_id);

    switch (instr.op) {
    case Opcode::add:
    case Opcode::addi:
    case Opcode::sub:
    case Opcode::movi:
    case Opcode::movr:
    case Opcode::movp:
    case Opcode::nop:
        // ALU operations - check ALU0 then ALU1
        if (m_bundles.at(time)[0] == nullptr) {
            // ALU0 is available
            m_bundles.at(time)[0] = &instr;
            time_table[instr_id] = time;
            return true;
        }
        if (m_bundles.at(time)[1] == nullptr) {
            // ALU1 is available
            m_bundles.at(time)[1] = &instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::mulu:
        // Multiplication operation - check MUL unit
        if (m_bundles.at(time)[2] == nullptr) {
            m_bundles.at(time)[2] = &instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::ld:
    case Opcode::st:
        // Memory operations - check MEM unit
        if (m_bundles.at(time)[3] == nullptr) {
            m_bundles.at(time)[3] = &instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::loop:
    case Opcode::loop_pip:
        // Branch operations - check BRANCH unit
        if (m_bundles.at(time)[4] == nullptr) {
            m_bundles.at(time)[4] = &instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    }

    return false;
}

/**
 * Schedules the loop body with pipeline support
 * Implements modulo scheduling with resource reservation
 * Will retry with increased II if scheduling fails
 */
void LoopPipCompiler::schedule_loop_body_pipelined(std::vector<uint64_t>& time_table) {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // If BB1 is empty, just set markers and return
    if (basic_blocks[1].first >= basic_blocks[1].second) {
        m_loop_start_time = m_bundles.size();
        m_loop_end_time = m_bundles.size();
		return;
    }
    
    // Collect all instruction IDs in the loop body
    std::vector<uint64_t> loop_instructions;
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second; ++i) {
        loop_instructions.push_back(i);
    }
    
    // Calculate lowest possible time to start the loop based on dependencies
    uint64_t lowest_start_time = calculate_loop_start_time(dependencies, basic_blocks[1], time_table);
    
    // Iteratively try to schedule with increasing II values
    while (true) {
        // Remember current bundle size in case we need to retry
        uint64_t saved_bundle_size = m_bundles.size();
        bool scheduling_success = true;
        
        // Try to schedule all instructions except the last one (loop instruction)
        for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
            uint64_t lowest_time = calculate_instruction_earliest_time(i, dependencies, time_table, lowest_start_time);
            
            // Try to insert with modulo scheduling
            if (!try_modulo_insert(i, lowest_time, time_table)) {
                create_new_bundle_with_reservations(i, lowest_time, time_table);
            }
        }
        
        // Handle loop.pip instruction specially
        uint64_t loop_instr_idx = basic_blocks[1].second - 1;
        
        // Store loop start address in the instruction
        Instruction& loop_instr = const_cast<Instruction&>(m_program[loop_instr_idx]);
        loop_instr.imm = lowest_start_time;
        
        // Place the loop.pip instruction at the end of the current scheduling
        uint64_t loop_time = m_bundles.size() + calculate_time_after_loop(dependencies, loop_instructions, time_table, lowest_start_time);
        
        if (!try_modulo_insert(loop_instr_idx, loop_time, time_table)) {
            create_new_bundle_with_reservations(loop_instr_idx, loop_time, time_table);
        }
        
        // Check if all interloop dependencies are satisfied
        if (verify_pipeline_dependencies(time_table, loop_instructions)) {
            // Valid schedule found
            break;
        } else {
            // Increase II and try again
            m_initiation_interval++;
            
            // Restore bundle size
            while (m_bundles.size() > saved_bundle_size) {
                m_bundles.pop_back();
                m_slot_status.pop_back();
            }
            
            // Clear time table entries for loop instructions
            for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second; ++i) {
                time_table[i] = UINT64_MAX;
            }
        }
    }
    
    // Ensure loop body length is a multiple of II
    while ((m_bundles.size() - m_loop_start_time) % m_initiation_interval != 0) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
        m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
        update_resource_reservations();
    }
}

/**
 * Calculate the earliest possible start time for the loop based on dependencies
 */
uint64_t LoopPipCompiler::calculate_loop_start_time(const std::vector<Dependency>& dependencies, 
                                                 const Block& loop_block,
                                                 const std::vector<uint64_t>& time_table) {
    uint64_t lowest_time = m_bundles.size();
    
    // Process loop instructions except the last one (loop instruction)
    for (uint64_t i = loop_block.first; i < loop_block.second - 1; ++i) {
        // Check loop invariant dependencies
        for (uint64_t dep_id : dependencies[i].loop_invariant) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Check interloop dependencies from BB0
        for (uint64_t dep_id : dependencies[i].interloop) {
            if (dep_id < loop_block.first) {  // Only consider BB0 dependencies here
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
            }
        }
    }
    
    return lowest_time;
}

/**
 * Calculate the earliest possible time for an instruction based on all its dependencies
 */
uint64_t LoopPipCompiler::calculate_instruction_earliest_time(uint64_t instr_id, 
                                                          const std::vector<Dependency>& dependencies,
                                                          const std::vector<uint64_t>& time_table,
                                                          uint64_t loop_start_time) {
    uint64_t lowest_time = loop_start_time;
    
    // Check local dependencies
    for (uint64_t dep_id : dependencies[instr_id].local) {
        uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
        if (time_table[dep_id] != UINT64_MAX) {
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
    }
    
    // Other dependency types are handled at loop level in calculate_loop_start_time
    
    return lowest_time;
}

/**
 * Calculate additional time needed after the loop to handle interloop dependencies
 */
uint64_t LoopPipCompiler::calculate_time_after_loop(const std::vector<Dependency>& dependencies,
                                                 const std::vector<uint64_t>& loop_instructions,
                                                 const std::vector<uint64_t>& time_table,
                                                 uint64_t loop_start_time) {
    uint64_t time_needed = 0;
    
    // Check all interloop dependencies within the loop body
    for (uint64_t i : loop_instructions) {
        for (uint64_t dep_id : dependencies[i].interloop) {
            // Only consider dependencies within the loop body
            if (std::find(loop_instructions.begin(), loop_instructions.end(), dep_id) != loop_instructions.end()) {
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                
                // Calculate time from loop dependency to end of loop
                uint64_t time_after = m_bundles.size() - time_table[dep_id];
                
                // Time from start of loop to the consuming instruction
                uint64_t time_before = time_table[i] - loop_start_time;
                
                // Time needed is latency - time_after - time_before
                int64_t needed = latency - time_after - time_before;
                if (needed > 0) {
                    time_needed = std::max(time_needed, static_cast<uint64_t>(needed));
                }
            }
        }
    }
    
    return time_needed;
}

/**
 * Schedules the post-loop code
 * Similar to non-pipelined version but respects pipeline dependencies
 */
void LoopPipCompiler::schedule_postloop_block(std::vector<uint64_t>& time_table) {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // Process each instruction in BB2
    for (uint64_t i = basic_blocks[2].first; i < basic_blocks[2].second; ++i) {
        // Start from the current end of schedule
        uint64_t lowest_time = m_bundles.size();
        
        // Check all dependency types
        // Loop invariant dependencies
        for (uint64_t dep_id : dependencies[i].loop_invariant) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            if (time_table[dep_id] != UINT64_MAX) {
                lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
            }
        }
        
        // Post-loop dependencies
        for (uint64_t dep_id : dependencies[i].post_loop) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            if (time_table[dep_id] != UINT64_MAX) {
                lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
            }
        }
        
        // Local dependencies
        for (uint64_t dep_id : dependencies[i].local) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            if (time_table[dep_id] != UINT64_MAX) {
                lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
            }
        }
        
        // Try to insert ASAP, if not possible, append a new bundle
        // We don't need modulo scheduling after the loop
        schedule_asap(time_table, i, lowest_time);
    }
}

/**
 * Attempts to insert an instruction with modulo scheduling
 * Reserves slots in other iterations according to the II
 */
bool LoopPipCompiler::try_modulo_insert(uint64_t instr_id, uint64_t earliest_time,
                                     std::vector<uint64_t>& time_table) {
    const auto& instr = m_program[instr_id];
    
    // Make sure we have enough bundles to consider
    while (m_bundles.size() <= earliest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
        m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
    }
    
    // Try each bundle starting from the earliest possible time
    for (uint64_t bundle_idx = earliest_time; bundle_idx < m_bundles.size(); ++bundle_idx) {
        int slot_idx = -1;
        
        // Determine which slot to use based on instruction type
        if (instr.op == Opcode::add || instr.op == Opcode::addi || 
            instr.op == Opcode::sub || instr.op == Opcode::movi || 
            instr.op == Opcode::movr || instr.op == Opcode::movp || 
            instr.op == Opcode::nop) {
            // ALU operations
            if (m_bundles[bundle_idx][0] == nullptr && m_slot_status[bundle_idx][0] == OPEN) {
                slot_idx = 0; // ALU0
            } else if (m_bundles[bundle_idx][1] == nullptr && m_slot_status[bundle_idx][1] == OPEN) {
                slot_idx = 1; // ALU1
            }
        } else if (instr.op == Opcode::mulu) {
            // Multiplication operation
            if (m_bundles[bundle_idx][2] == nullptr && m_slot_status[bundle_idx][2] == OPEN) {
                slot_idx = 2; // MUL
            }
        } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
            // Memory operations
            if (m_bundles[bundle_idx][3] == nullptr && m_slot_status[bundle_idx][3] == OPEN) {
                slot_idx = 3; // MEM
            }
        } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
            // Branch operations
            if (m_bundles[bundle_idx][4] == nullptr && m_slot_status[bundle_idx][4] == OPEN) {
                slot_idx = 4; // BRANCH
            }
        }
        
        if (slot_idx >= 0) {
            // Found a suitable slot
            m_bundles[bundle_idx][slot_idx] = &instr;
            time_table[instr_id] = bundle_idx;
            
            // Reserve slots at II-distance bundles
            for (uint64_t other_idx = m_loop_start_time; other_idx < m_bundles.size(); ++other_idx) {
                // If bundle is at II-distance and not the current bundle
                if (other_idx != bundle_idx && (other_idx - bundle_idx) % m_initiation_interval == 0) {
                    m_slot_status[other_idx][slot_idx] = RESERVED;
                }
            }
            
            return true;
        }
    }
    
    return false;
}

/**
 * Appends a new bundle with modulo scheduling support
 * Used when insertion fails
 * Also handles resource reservation
 */
void LoopPipCompiler::create_new_bundle_with_reservations(uint64_t instr_id, uint64_t earliest_time,
                                                       std::vector<uint64_t>& time_table) {
    const auto& instr = m_program[instr_id];
    
    // Make sure we have enough bundles up to earliest_time
    while (m_bundles.size() <= earliest_time) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
        m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
    }
    
    // Create a new bundle
    m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    m_slot_status.push_back({OPEN, OPEN, OPEN, OPEN, OPEN});
    
    // Update reservations for all existing bundles
    update_resource_reservations();
    
    uint64_t bundle_idx = m_bundles.size() - 1;
    
    // Try to insert in the appropriate slot
    int slot_idx = -1;
    
    // Determine which slot to use based on instruction type
    if (instr.op == Opcode::add || instr.op == Opcode::addi || 
        instr.op == Opcode::sub || instr.op == Opcode::movi || 
        instr.op == Opcode::movr || instr.op == Opcode::movp || 
        instr.op == Opcode::nop) {
        // ALU operations
        slot_idx = 0; // ALU0
    } else if (instr.op == Opcode::mulu) {
        // Multiplication operation
        slot_idx = 2; // MUL
    } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
        // Memory operations
        slot_idx = 3; // MEM
    } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
        // Branch operations
        slot_idx = 4; // BRANCH
    }
    
    // Assign instruction to slot
    m_bundles[bundle_idx][slot_idx] = &instr;
    time_table[instr_id] = bundle_idx;
    
    // Reserve slots at II-distance bundles
    for (uint64_t other_idx = m_loop_start_time; other_idx < m_bundles.size(); ++other_idx) {
        // If bundle is at II-distance and not the current bundle
        if (other_idx != bundle_idx && (other_idx - bundle_idx) % m_initiation_interval == 0) {
            m_slot_status[other_idx][slot_idx] = RESERVED;
        }
    }
}

/**
 * Propagates resource reservations when adding new bundles
 * Ensures that reserved slots maintain consistency across the schedule
 */
void LoopPipCompiler::update_resource_reservations() {
    // Get the index of the newly added bundle
    uint64_t new_bundle_idx = m_bundles.size() - 1;
    
    // For each bundle in the loop
    for (uint64_t bundle_idx = m_loop_start_time; bundle_idx < new_bundle_idx; ++bundle_idx) {
        // If the new bundle is at II-distance from the current bundle
        if ((new_bundle_idx - bundle_idx) % m_initiation_interval == 0) {
            // Check each slot
            for (int slot_idx = 0; slot_idx < 5; ++slot_idx) {
                // If a slot in the existing bundle is occupied, reserve it in the new bundle
                if (m_bundles[bundle_idx][slot_idx] != nullptr) {
                    m_slot_status[new_bundle_idx][slot_idx] = RESERVED;
                }
                
                // If a slot in the new bundle is occupied, reserve it in the existing bundle
                if (m_bundles[new_bundle_idx][slot_idx] != nullptr) {
                    m_slot_status[bundle_idx][slot_idx] = RESERVED;
                }
            }
        }
    }
}

/**
 * Checks if all interloop dependencies are satisfied with current II
 * Verifies the equation: S(P) + λ(P) ≤ S(C) + II
 */
bool LoopPipCompiler::verify_pipeline_dependencies(const std::vector<uint64_t>& time_table,
                                               const std::vector<uint64_t>& loop_instructions) {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // Check each instruction in the loop
    for (uint64_t instr_id : loop_instructions) {
        // Check all interloop dependencies
        for (uint64_t dep_id : dependencies[instr_id].interloop) {
            // Only consider dependencies within the loop body
            if (std::find(loop_instructions.begin(), loop_instructions.end(), dep_id) != loop_instructions.end()) {
                // Get scheduled times
                uint64_t producer_time = time_table[dep_id];
                uint64_t consumer_time = time_table[instr_id];
                
                // Get latency
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                
                // Verify the equation: S(P) + λ(P) ≤ S(C) + II
                if (producer_time + latency > consumer_time + m_initiation_interval) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

/**
 * Organizes loop bundles into pipeline stages
 * Each stage has II bundles
 * Used for final code generation and predication
 */
void LoopPipCompiler::organize_pipeline_stages() {
    // TODO: Implement this function to:
    // 1. Clear existing pipeline stages
    // 2. Calculate how many stages there are (loop length / II)
    // 3. Assign each bundle to its appropriate stage
    // 4. This will be used for predication later
    
    // Clear existing stages
    m_pipeline_stages.clear();
    
    // Calculate number of stages
    uint64_t loop_length = m_loop_end_time - m_loop_start_time;
    uint64_t num_stages = (loop_length + m_initiation_interval - 1) / m_initiation_interval;
    
    // Initialize stages
    m_pipeline_stages.resize(num_stages);
    
    // Assign bundles to stages
    uint64_t stage_idx = 0;
    uint64_t bundle_in_stage = 0;
    
    for (uint64_t bundle_idx = m_loop_start_time; bundle_idx < m_loop_end_time; ++bundle_idx) {
        // Add this bundle to the current stage
        m_pipeline_stages[stage_idx].push_back(bundle_idx);
        
        // Move to next stage after II bundles
        bundle_in_stage++;
        if (bundle_in_stage == m_initiation_interval) {
            stage_idx++;
            bundle_in_stage = 0;
        }
    }
}

/**
 * Assigns predicate registers to instructions based on pipeline stage
 * First stage gets p32, second p33, etc.
 */
void LoopPipCompiler::assign_predicate_registers() {
    // TODO: Implement this function to:
    // 1. Initialize predicate map to hold one predicate ID per instruction
    // 2. For each stage, assign a predicate register to all instructions
    // 3. Use p32 for stage 0, p33 for stage 1, etc.
    // This will be needed for the final code generation
    
    // Initialize predicate map
    m_predicate_map.resize(m_program.size(), UINT32_MAX);
    
    // For each stage
    for (uint64_t stage_idx = 0; stage_idx < m_pipeline_stages.size(); ++stage_idx) {
        // Predicate register for this stage (p32 + stage_idx)
        uint32_t predicate_reg = 32 + stage_idx;
        
        // For each bundle in this stage
        for (uint64_t bundle_idx : m_pipeline_stages[stage_idx]) {
            // For each instruction in this bundle
            for (int slot_idx = 0; slot_idx < 5; ++slot_idx) {
                const Instruction* instr = m_bundles[bundle_idx][slot_idx];
                if (instr != nullptr) {
                    // Get the instruction ID
                    uint64_t instr_id = instr - &m_program[0];
                    
                    // Assign predicate register
                    m_predicate_map[instr_id] = predicate_reg;
                }
            }
        }
    }
}

/**
 * Creates initialization code for predicates and EC register
 * Adds mov instructions before the loop.pip instruction
 */
void LoopPipCompiler::setup_pipeline_initialization() {
    // TODO: Implement this function to:
    // 1. Find the bundle just before the loop.pip instruction
    // 2. Add a mov instruction to initialize p32 to true
    // 3. Add a mov instruction to initialize EC to (num_stages - 1)
    // 4. If no space in existing bundle, create a new one
    
    // This is required for the pipeline execution to work correctly
    
    // For now, we leave this as a placeholder
    // The full implementation would require creating new instructions
    // and inserting them into the schedule
}

void LoopPipCompiler::rename(
    const std::vector<uint64_t> time_table,
    const std::vector<Dependency> dependencies) {
    // copy the bundles
    auto bundles = std::vector<std::array<Instruction, 5>>{};
    for (const auto& bundle : m_bundles) {
        std::array<Instruction, 5> new_bundle;
        new_bundle[0] = bundle[0] ? *bundle[0] : Instruction{Opcode::nop};
        new_bundle[1] = bundle[1] ? *bundle[1] : Instruction{Opcode::nop};
        new_bundle[2] = bundle[2] ? *bundle[2] : Instruction{Opcode::nop};
        new_bundle[3] = bundle[3] ? *bundle[3] : Instruction{Opcode::nop};
        new_bundle[4] = bundle[4] ? *bundle[4] : Instruction{Opcode::nop};
        bundles.push_back(new_bundle);
    }

    std::vector<uint32_t> non_rotating_registers(num_non_rotating_registers);
    std::vector<uint32_t> rotating_registers(num_non_rotating_registers);
    std::generate(non_rotating_registers.begin(), non_rotating_registers.end(), [i = 0]() mutable {
        return i++;
    });
    std::generate(rotating_registers.begin(), rotating_registers.end(), [i = 0]() mutable {
        return num_non_rotating_registers + i++;
    });
}