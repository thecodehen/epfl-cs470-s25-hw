#include "loop_pip_compiler.h"

#include <algorithm>
#include <iostream>

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

    // rename registers
    auto bundles = rename(time_table, basic_blocks, dependencies);

    if (basic_blocks.size() > 1) {
        // Assign predicate registers to instructions based on stage
        assign_predicate_registers(bundles);

        compress_pipeline(bundles);

        // Add initialization instructions for predicates and EC
        setup_pipeline_initialization(bundles);
    }

    // Create the final VLIW program
    VLIWProgram program;

    // For each bundle, create a VLIW instruction with all functional units
    for (const auto& bundle : bundles) {
        program.alu0_instructions.push_back(bundle[0]);
        program.alu1_instructions.push_back(bundle[1]);
        program.mult_instructions.push_back(bundle[2]);
        program.mem_instructions.push_back(bundle[3]);
        program.branch_instructions.push_back(bundle[4]);
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
        schedule_loop_body(time_table, basic_blocks.at(1), dependencies);
        m_loop_end_time = m_bundles.size(); // Mark end of loop

        // Finally, schedule post-loop code (BB2)
        if (basic_blocks.size() > 2) {
            schedule_postloop_block(time_table);
        }

        // Organize the pipeline stages
        organize_pipeline_stages();
    }

    /*
    std::cout << "m_loop_start_time: " << m_loop_start_time << std::endl;
    std::cout << "m_loop_end_time: " << m_loop_end_time << std::endl;
    std::cout << "m_initiation_interval: " << m_initiation_interval << std::endl;
    // Create the final VLIW program
    VLIWProgram program;

    // For each bundle, create a VLIW instruction with all functional units
    for (const auto bundle : m_bundles) {
        program.alu0_instructions.push_back(bundle[0] ? *bundle[0] : Instruction{Opcode::nop});
        program.alu1_instructions.push_back(bundle[1] ? *bundle[1] : Instruction{Opcode::nop});
        program.mult_instructions.push_back(bundle[2] ? *bundle[2] : Instruction{Opcode::nop});
        program.mem_instructions.push_back(bundle[3] ? *bundle[3] : Instruction{Opcode::nop});
        program.branch_instructions.push_back(bundle[4] ? *bundle[4] : Instruction{Opcode::nop});
    }

    program.print();
    std::cout << std::endl;
    */

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
    // Make sure we have enough bundles to consider
    if (m_bundles.size() <= lowest_time) {
        m_bundles.resize(lowest_time + 1);
    }

    // Try each bundle starting from the lowest possible time
    for (auto i_bundle {lowest_time}; i_bundle < m_bundles.size(); ++i_bundle) {
        if (try_schedule(time_table, instr_id, i_bundle)) {
            return;
        }
    }

    // no suitable slot found in existing bundle, need to create a new bundle
    m_bundles.push_back({});
    try_schedule(time_table, instr_id, m_bundles.size() - 1);
}

bool LoopPipCompiler::try_schedule(
    std::vector<uint64_t>& time_table,
    const uint64_t instr_id,
    const uint64_t time
    ) {
    // Determine instruction type and check appropriate functional unit
    const auto instr = &m_program.at(instr_id);

    switch (instr->op) {
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
            m_bundles.at(time)[0] = instr;
            time_table[instr_id] = time;
            return true;
        }
        if (m_bundles.at(time)[1] == nullptr) {
            // ALU1 is available
            m_bundles.at(time)[1] = instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::mulu:
        // Multiplication operation - check MUL unit
        if (m_bundles.at(time)[2] == nullptr) {
            m_bundles.at(time)[2] = instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::ld:
    case Opcode::st:
        // Memory operations - check MEM unit
        if (m_bundles.at(time)[3] == nullptr) {
            m_bundles.at(time)[3] = instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::loop:
    case Opcode::loop_pip:
        // Branch operations - check BRANCH unit
        if (m_bundles.at(time)[4] == nullptr) {
            m_bundles.at(time)[4] = instr;
            time_table[instr_id] = time;
            return true;
        }
        break;
    }

    return false;
}

bool LoopPipCompiler::check_slot_available(
    const uint64_t instr_id
) const
{
    switch (const auto& instr = m_program.at(instr_id); instr.op) {
    case Opcode::add:
    case Opcode::addi:
    case Opcode::sub:
    case Opcode::movi:
    case Opcode::movr:
    case Opcode::movp:
    case Opcode::nop:
        for (const auto& slot : m_slot_status) {
            if (slot[0] == OPEN || slot[1] == OPEN) {
                return true;
            }
        }
        return false;
    case Opcode::mulu:
        for (const auto& slot : m_slot_status) {
            if (slot[2] == OPEN) {
                return true;
            }
        }
        return false;
    case Opcode::ld:
    case Opcode::st:
        for (const auto& slot : m_slot_status) {
            if (slot[3] == OPEN) {
                return true;
            }
        }
        return false;
    case Opcode::loop:
    case Opcode::loop_pip:
        for (const auto& slot : m_slot_status) {
            if (slot[4] == OPEN) {
                return true;
            }
        }
        return false;
    }

    return false;
}

bool LoopPipCompiler::try_schedule_modulo(
    std::vector<uint64_t>& time_table,
    const uint64_t instr_id,
    const uint64_t time
    ) {
    // Determine instruction type and check appropriate functional unit
    const auto instr = &m_program.at(instr_id);

    // Make sure we are scheduling the loop block
    assert(time >= m_loop_start_time);
    const auto status_index {(time - m_loop_start_time) % m_initiation_interval};

    switch (instr->op) {
    case Opcode::add:
    case Opcode::addi:
    case Opcode::sub:
    case Opcode::movi:
    case Opcode::movr:
    case Opcode::movp:
    case Opcode::nop:
        // ALU operations - check ALU0 then ALU1
        if (m_bundles.at(time)[0] == nullptr && m_slot_status.at(status_index)[0] == OPEN) {
            // ALU0 is available
            m_bundles.at(time)[0] = instr;
            m_slot_status.at(status_index)[0] = RESERVED;
            time_table[instr_id] = time;
            return true;
        }
        if (m_bundles.at(time)[1] == nullptr && m_slot_status.at(status_index)[1] == OPEN) {
            // ALU1 is available
            m_bundles.at(time)[1] = instr;
            m_slot_status.at(status_index)[1] = RESERVED;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::mulu:
        // Multiplication operation - check MUL unit
        if (m_bundles.at(time)[2] == nullptr && m_slot_status.at(status_index)[2] == OPEN) {
            m_bundles.at(time)[2] = instr;
            m_slot_status.at(status_index)[2] = RESERVED;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::ld:
    case Opcode::st:
        // Memory operations - check MEM unit
        if (m_bundles.at(time)[3] == nullptr && m_slot_status.at(status_index)[3] == OPEN) {
            m_bundles.at(time)[3] = instr;
            m_slot_status.at(status_index)[3] = RESERVED;
            time_table[instr_id] = time;
            return true;
        }
        break;
    case Opcode::loop:
    case Opcode::loop_pip:
        // Branch operations - check BRANCH unit
        if (m_bundles.at(time)[4] == nullptr && m_slot_status.at(status_index)[4] == OPEN) {
            m_bundles.at(time)[4] = instr;
            m_slot_status.at(status_index)[4] = RESERVED;
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
void LoopPipCompiler::schedule_loop_body(
    std::vector<uint64_t>& time_table,
    const Block& bb1,
    const std::vector<Dependency>& dependencies
    ) {
    // Calculate lowest possible time to start the loop based on dependencies
    const auto lowest_start_time = calculate_loop_start_time(
        time_table,
        dependencies,
        bb1
    );
    m_loop_start_time = lowest_start_time;

    // Remember current bundle size in case we need to retry
    const auto saved_bundle_size = m_bundles.size();

    // Iteratively try to schedule with increasing II values
    while (true) {
        // Restore bundle size
        m_bundles.resize(saved_bundle_size);

        // Fill slot status with OPEN slots
        m_slot_status.resize(m_initiation_interval);
        constexpr auto open_slot_status {std::array<SlotStatus, 5>{OPEN, OPEN, OPEN, OPEN, OPEN}};
        std::fill(m_slot_status.begin(), m_slot_status.end(), open_slot_status);

        // Try to schedule all instructions except the last one (loop instruction)
        bool success {true};
        for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
            const auto lowest_time = calculate_instruction_earliest_time(i, dependencies, time_table, lowest_start_time);
            std::cout << "lowest_time for instruction " << i << ": " << lowest_time << std::endl;

            // Try to insert with modulo scheduling
            if (!schedule_asap_modulo(time_table, i, lowest_time)) {
                success = false;
                break;
            }
        }

        if (success) {
            // Handle loop.pip instruction specially
            const auto loop_instr_idx = bb1.second - 1;

            // Store loop start address in the instruction
            auto& loop_instr = m_program.at(loop_instr_idx);
            loop_instr.op = Opcode::loop_pip;
            loop_instr.imm = m_loop_start_time;

            // Place the loop.pip instruction at the end of the current scheduling
            // uint64_t loop_time = m_bundles.size() + calculate_time_after_loop(dependencies, loop_instructions, time_table, lowest_start_time);

            // if (!schedule_asap_modulo(time_table, loop_instr_idx, loop_time)) {
            //     success = false;
            // }

            // schedule the loop at the end of stage 0
            const auto loop_instr_bundle_id = m_loop_start_time + m_initiation_interval - 1;
            if (m_bundles.size() <= loop_instr_bundle_id) {
                m_bundles.resize(loop_instr_bundle_id + 1);
            }
            m_bundles.at(loop_instr_bundle_id)[4] = &m_program.at(loop_instr_idx);

            // Check if all interloop dependencies are satisfied
            if (verify_pipeline_dependencies(time_table, dependencies, bb1)) {
                // Valid schedule found
                break;
            }
        }

        // Increase II and try again
        m_initiation_interval++;

        // Clear time table entries for loop instructions
        for (uint64_t i = bb1.first; i < bb1.second; ++i) {
            time_table[i] = UINT64_MAX;
        }
    }

    // Ensure loop body length is a multiple of II
    while ((m_bundles.size() - m_loop_start_time) % m_initiation_interval != 0) {
        m_bundles.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    }
}

/**
 * Calculate the earliest possible start time for the loop based on dependencies
 */
uint64_t LoopPipCompiler::calculate_loop_start_time(
        const std::vector<uint64_t>& time_table,
        const std::vector<Dependency>& dependencies,
        const Block& loop_block
        ) {
    uint64_t lowest_time = m_bundles.size();

    // Process loop instructions except the last one (loop instruction)
    for (auto i {loop_block.first}; i < loop_block.second - 1; ++i) {
        // Check loop invariant dependencies
        for (const auto dep_id : dependencies[i].loop_invariant) {
            const auto latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }

        // Check interloop dependencies from BB0
        for (const auto dep_id : dependencies[i].interloop) {
            if (dep_id < loop_block.first) {  // Only consider BB0 dependencies here
                const auto latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
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
bool LoopPipCompiler::schedule_asap_modulo(
    std::vector<uint64_t>& time_table,
    const uint64_t instr_id,
    const uint64_t earliest_time)
{
    // Make sure we have enough bundles to consider
    if (m_bundles.size() <= earliest_time) {
        m_bundles.resize(earliest_time + 1);
    }

    // Try each bundle starting from the earliest possible time
    for (auto time {earliest_time}; check_slot_available(instr_id); ++time) {
        if (try_schedule_modulo(time_table, instr_id, time)) {
            return true;
        }

        // no suitable slot found in existing bundle, need to create a new bundle
        m_bundles.push_back({});
    }

    return false;
}

/**
 * Checks if all interloop dependencies are satisfied with current II
 * Verifies the equation: S(P) + λ(P) ≤ S(C) + II
 */
bool LoopPipCompiler::verify_pipeline_dependencies(
    const std::vector<uint64_t>& time_table,
    const std::vector<Dependency>& dependencies,
    const Block& bb1
    ) {
    // Check each instruction in the loop
    for (auto instr_id {bb1.first}; instr_id < bb1.second; ++instr_id) {
        // Check all interloop dependencies
        for (const auto dep_id : dependencies.at(instr_id).interloop) {
            // Only consider dependencies within the loop body
            if (bb1.first <= dep_id && dep_id < bb1.second) {
                // Get scheduled times
                const auto producer_time = time_table[dep_id];
                const auto consumer_time = time_table[instr_id];

                // Get latency
                const auto latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;

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
    const auto loop_length = m_loop_end_time - m_loop_start_time;
    const auto num_stages = (loop_length + m_initiation_interval - 1) / m_initiation_interval;

    // Initialize stages
    m_pipeline_stages.resize(num_stages);

    // Assign bundles to stages
    uint64_t stage_idx = 0;
    uint64_t bundle_in_stage = 0;

    for (auto bundle_idx {m_loop_start_time}; bundle_idx < m_loop_end_time; ++bundle_idx) {
        // Add this bundle to the current stage
        m_pipeline_stages[stage_idx].push_back(bundle_idx);

        for (const auto instr : m_bundles.at(bundle_idx)) {
            if (instr != nullptr) {
                m_instruction_to_stage_map[instr->id] = stage_idx;
            }
        }

        // Move to next stage after II bundles
        bundle_in_stage++;
        if (bundle_in_stage == m_initiation_interval) {
            stage_idx++;
            bundle_in_stage = 0;
        }
    }

    std::cout << "Pipeline stages organized into " << num_stages << " stages." << std::endl;
    for (const auto& stage : m_pipeline_stages) {
        std::cout << "Stage: ";
        for (const auto& bundle : stage) {
            std::cout << bundle << " ";
        }
        std::cout << std::endl;
    }
}

/**
 * Assigns predicate registers to instructions based on pipeline stage
 * First stage gets p32, second p33, etc.
 */
void LoopPipCompiler::assign_predicate_registers(
    std::vector<Bundle>& bundles
) {
    // TODO: Implement this function to:
    // 1. Initialize predicate map to hold one predicate ID per instruction
    // 2. For each stage, assign a predicate register to all instructions
    // 3. Use p32 for stage 0, p33 for stage 1, etc.
    // This will be needed for the final code generation

    // Initialize predicate map
    m_predicate_map.resize(m_program.size(), UINT32_MAX);

    // For each stage
    for (auto stage_idx {0}; stage_idx < m_pipeline_stages.size(); ++stage_idx) {
        // Predicate register for this stage (p32 + stage_idx)
        const auto predicate_reg {num_non_rotating_registers + stage_idx};

        // For each bundle in this stage
        for (const auto bundle_idx : m_pipeline_stages[stage_idx]) {
            // For each instruction in this bundle
            for (auto& instr : bundles.at(bundle_idx)) {
                if (instr.op != Opcode::nop && instr.op != Opcode::loop_pip) {
                    instr.pred = predicate_reg;
                }
            }
        }
    }
}

/**
 * Creates initialization code for predicates and EC register
 * Adds mov instructions before the loop.pip instruction
 */
void LoopPipCompiler::setup_pipeline_initialization(std::vector<Bundle>& bundles) const
{
    // TODO: Implement this function to:
    // 1. Find the bundle just before the loop.pip instruction
    // 2. Add a mov instruction to initialize p32 to true
    // 3. Add a mov instruction to initialize EC to (num_stages - 1)
    // 4. If no space in existing bundle, create a new one

    // This is required for the pipeline execution to work correctly

    // For now, we leave this as a placeholder
    // The full implementation would require creating new instructions
    // and inserting them into the schedule
    std::vector instr_to_schedule {
        Instruction {
            .op = Opcode::movp,
            .dest = num_non_rotating_registers,
            .imm = 1 // true
        },
        Instruction {
            .op = Opcode::movi,
            .dest = ec_id,
            .imm = static_cast<int64_t>(m_pipeline_stages.size() - 1)
        },
    };
    // m_loop_start_time should not be 0 since the user always need to specify
    // LC, which takes at least one instruction
    assert(m_loop_start_time > 0);
    auto& bundle_before_loop {bundles.at(m_loop_start_time - 1)};
    if (bundle_before_loop.at(0).op == Opcode::nop) {
        bundle_before_loop.at(0) = instr_to_schedule.back();
        instr_to_schedule.pop_back();
    }
    if (bundle_before_loop.at(1).op == Opcode::nop) {
        bundle_before_loop.at(1) = instr_to_schedule.back();
        instr_to_schedule.pop_back();
    }
    if (!instr_to_schedule.empty()) {
        // need to add a new bundle and adjust the loop start time
        constexpr Bundle empty_bundle {
            Instruction {.op = Opcode::nop},
            Instruction {.op = Opcode::nop},
            Instruction {.op = Opcode::nop},
            Instruction {.op = Opcode::nop},
            Instruction {.op = Opcode::nop}
        };
        bundles.insert(std::next(bundles.begin(), m_loop_start_time), empty_bundle);

        // bundle_before_loop no longer valid, get a new one
        auto& new_bundle_before_loop = bundles.at(m_loop_start_time);
        new_bundle_before_loop.at(0) = instr_to_schedule.back();
        instr_to_schedule.pop_back();
        if (!instr_to_schedule.empty()) {
            new_bundle_before_loop.at(1) = instr_to_schedule.back();
        }

        // change the loop start time
        for (auto& bundle : bundles) {
            if (bundle[4].op == Opcode::loop_pip) {
                bundle[4].imm += 1;
                break;
            }
        }
    }
}

void LoopPipCompiler::compress_pipeline(std::vector<Bundle>& bundles) const
{
    const auto num_stages {m_pipeline_stages.size()};

    // copy the instructions to the first stage, will need to set the predicates
    // later
    for (auto stage_idx {1}; stage_idx < num_stages; ++stage_idx) {
        for (const auto bundle_idx : m_pipeline_stages[stage_idx]) {
            const auto& bundle_from {bundles.at(bundle_idx)};
            auto& bundle_to {bundles.at(bundle_idx - m_initiation_interval * stage_idx)};
            for (auto i {0}; i < bundle_from.size(); ++i) {
                if (bundle_from.at(i).op != Opcode::nop) {
                    assert(bundle_to.at(i).op == Opcode::nop);
                    bundle_to.at(i) = bundle_from.at(i);
                }
            }
        }
    }

    // remove the instructions from later stages
    bundles.erase(
        bundles.begin() + m_loop_start_time + m_initiation_interval,
        bundles.begin() + m_loop_end_time
    );
}

std::vector<Bundle> LoopPipCompiler::rename(
        const std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies
        ) {
    const bool has_loop {basic_blocks.size() > 1};
    if (has_loop) {
        rename_loop_body_dest();
    }
    rename_loop_invariant(dependencies);
    // only rename BB1 and BB2 if needed
    if (has_loop) {
        rename_loop_body_consumer(dependencies, basic_blocks.at(1));
        rename_post_loop_consumer(dependencies, basic_blocks.at(2));
    }
    rename_non_loop(dependencies, basic_blocks.at(0));
    if (has_loop) {
        rename_non_loop(dependencies, basic_blocks.at(2));
    }
    rename_not_written_registers();

    // copy the bundles
    auto bundles = std::vector<std::array<Instruction, 5>>{};
    for (const auto& bundle : m_bundles) {
        std::array<Instruction, 5> new_bundle;
        for (int i = 0; i < 5; ++i) {
            new_bundle[i] = bundle[i] ? *bundle[i] : Instruction{Opcode::nop};
        }
        bundles.push_back(new_bundle);
    }
    return bundles;
}

void LoopPipCompiler::rename_loop_body_dest()
{
    const auto num_stages {m_pipeline_stages.size()};
    int32_t cur_reg {num_non_rotating_registers};
    for (auto bundle_i {m_loop_start_time}; bundle_i < m_loop_end_time; ++bundle_i) {
        auto& bundle {m_bundles.at(bundle_i)};
        for (const auto instr : bundle) {
            if (instr != nullptr && is_producer(instr->op)) {
                instr->new_dest = cur_reg;
                cur_reg += num_stages + 1;
            }
        }
    }
}

void LoopPipCompiler::rename_consumer_operands(
    const uint32_t old_dest,
    const uint32_t new_dest,
    Instruction& instr
    )
{
    if (instr.op_a == old_dest) {
        instr.op_a = new_dest;
        instr.has_op_a_been_renamed = true;
    }
    if (instr.op_b == old_dest) {
        instr.op_b = new_dest;
        instr.has_op_b_been_renamed = true;
    }
    // special handling for st because dest can be a consumer
    if (instr.op == Opcode::st && instr.dest == old_dest) {
        instr.dest = new_dest;
        instr.has_dest_been_renamed = true;
    }
}

void LoopPipCompiler::rename_loop_invariant(
    const std::vector<Dependency>& dependencies
    )
{
    // set of producer instruction ids that need to be renamed
    std::vector<uint64_t> id_to_be_renamed;
    for (const auto& dependency : dependencies) {
        for (auto id : dependency.loop_invariant) {
            if (std::find(id_to_be_renamed.begin(), id_to_be_renamed.end(), id) == id_to_be_renamed.end()) {
                id_to_be_renamed.push_back(id);
            }
        }
    }
    if (id_to_be_renamed.size() > num_non_rotating_registers) {
        std::cerr << "Error: too many non-rotating registers needed for loop "
                     "invariants\n";
    }
    assert(id_to_be_renamed.size() <= num_non_rotating_registers);

    // new_dest maps the old register id to the new register id
    std::unordered_map<uint32_t, uint32_t> new_dest;
    for (const auto id : id_to_be_renamed) {
        const auto& producer {m_program.at(id)};
        m_allocated_registers[m_next_non_rotating_reg] = true;
        new_dest[producer.dest] = m_next_non_rotating_reg++;
    }

    // rename the consumer instructions
    for (auto i {0}; i < m_program.size(); ++i) {
        auto& instr {m_program.at(i)};
        for (const auto producer_id : dependencies.at(i).loop_invariant) {
            const auto& producer_old_dest {m_program.at(producer_id).dest};

            rename_consumer_operands(producer_old_dest, new_dest[producer_old_dest], instr);
        }
    }

    // rename the producer instructions
    for (const auto id : id_to_be_renamed) {
        auto& instr {m_program.at(id)};

        // rename the dest register
        instr.new_dest = new_dest[instr.dest];
    }
}

void LoopPipCompiler::rename_loop_body_consumer(
    const std::vector<Dependency>& dependencies,
    const Block& bb1
    )
{
    for (auto instr_id {bb1.first}; instr_id < bb1.second; ++instr_id) {
        auto& instr {m_program.at(instr_id)};
        auto& dependency {dependencies.at(instr_id)};

        // adjust consumer registers for local dependencies
        for (auto dep : dependency.local) {
            const auto adjustment {m_instruction_to_stage_map.at(instr_id) - m_instruction_to_stage_map.at(dep)};
            const auto& instr_producer {m_program.at(dep)};
            rename_consumer_operands(instr_producer.dest, instr_producer.new_dest + adjustment, instr);
        }

        // adjust consumer registers for interloop dependencies
        for (const auto dep : dependency.interloop) {
            if (bb1.first <= dep && dep < bb1.second) {
                // dep from bb1
                const auto bb1_adjustment {m_instruction_to_stage_map.at(instr_id) - m_instruction_to_stage_map.at(dep) + 1};
                const auto& bb1_producer {m_program.at(dep)};
                rename_consumer_operands(bb1_producer.dest, bb1_producer.new_dest + bb1_adjustment, instr);

                // see if this dependency has another producer in BB0
                const auto dep_bb0 = std::find_if(dependency.interloop.begin(), dependency.interloop.end(), [&](const auto& d) {
                    const auto& bb0_producer {m_program.at(d)};
                    return d < bb1.first && bb0_producer.dest == bb1_producer.dest;
                });
                if (dep_bb0 != dependency.interloop.end()) {
                    // resolve dependency from BB0
                    const auto bb0_adjustment {-m_instruction_to_stage_map.at(dep) + 1};
                    auto& bb0_producer {m_program.at(*dep_bb0)};
                    bb0_producer.new_dest = bb1_producer.new_dest + bb0_adjustment;
                }
            }
        }
    }
}

void LoopPipCompiler::rename_post_loop_consumer(
    const std::vector<Dependency>& dependencies,
    const Block& bb2
    )
{
    for (auto instr_id {bb2.first}; instr_id < bb2.second; ++instr_id) {
        auto& instr {m_program.at(instr_id)};
        auto& dependency {dependencies.at(instr_id)};

        // adjust consumer registers for post_loop dependencies
        for (auto dep : dependency.post_loop) {
            // the instruction is considered to be in the last stage
            const auto adjustment {(m_pipeline_stages.size() - 1) - m_instruction_to_stage_map.at(dep)};
            const auto& instr_producer {m_program.at(dep)};
            rename_consumer_operands(instr_producer.dest, instr_producer.new_dest + adjustment, instr);
        }
    }
}

void LoopPipCompiler::rename_non_loop(
    const std::vector<Dependency>& dependencies,
    const Block& bb
) {
    for (auto& bundle : m_bundles) {
        for (const auto instr : bundle) {
            if (instr != nullptr &&
                // instr is in the block
                bb.first <= instr->id &&
                instr->id < bb.second &&
                // instr is a producer and dest is not renamed
                is_producer(instr->op) &&
                instr->new_dest == -1 &&
                // dest is not the special register
                instr->dest != lc_id
                ) {
                m_allocated_registers.at(m_next_non_rotating_reg) = true;
                instr->new_dest = m_next_non_rotating_reg++;
            }
        }
    }
    // fix local dependencies
    for (auto& bundle : m_bundles) {
        for (const auto instr : bundle) {
            if (instr != nullptr &&
                bb.first <= instr->id &&
                instr->id < bb.second
                ) {
                auto& dependency {dependencies.at(instr->id)};
                for (const auto dep : dependency.local) {
                    const auto& producer {m_program.at(dep)};
                    rename_consumer_operands(producer.dest, producer.new_dest, *instr);
                }
            }
        }
    }
}

void LoopPipCompiler::rename_not_written_registers()
{
    for (auto& bundle : m_bundles) {
        for (const auto instr : bundle) {
            if (instr != nullptr) {
                switch (instr->op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    if (!instr->has_op_a_been_renamed) {
                        instr->has_op_a_been_renamed = true;
                        instr->op_a = m_next_non_rotating_reg++;
                    }
                    if (!instr->has_op_b_been_renamed) {
                        instr->has_op_b_been_renamed = true;
                        instr->op_b = m_next_non_rotating_reg++;
                    }
                    break;
                case Opcode::addi:
                case Opcode::ld:
                case Opcode::movr:
                    if (!instr->has_op_a_been_renamed) {
                        instr->has_op_a_been_renamed = true;
                        instr->op_a = m_next_non_rotating_reg++;
                    }
                    break;
                case Opcode::st:
                    if (!instr->has_dest_been_renamed) {
                        instr->has_dest_been_renamed = true;
                        instr->dest = m_next_non_rotating_reg++;
                    }
                    if (!instr->has_op_a_been_renamed) {
                        instr->has_op_a_been_renamed = true;
                        instr->op_a = m_next_non_rotating_reg++;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
}