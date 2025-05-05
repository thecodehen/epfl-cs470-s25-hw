#include "loop_compiler.h"

#include <algorithm>
#include <bitset>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <memory>

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
	constexpr auto width {15};
	std::cout << std::setfill(' ')
		<< std::setw(width) << "instr|"
		<< std::setw(width) << "local|"
		<< std::setw(width) << "interloop|"
		<< std::setw(width) << "loop_invar|"
	    << std::setw(width) << "post_loop|" << '\n';
	std::cout << std::right << std::setfill('-') << std::setw(width * 5) << "" << '\n';
    for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
		std::cout << std::string(width - 6, ' ');
        std::cout << std::setfill('0') << std::setw(5)
            << std::distance(dependencies.begin(), it) << '|';
		// print local deps
		std::stringstream ss;
        for (auto i : it->local) {
            ss << i << " ";
        }
		ss << '|';
		std::cout << std::setfill(' ') << std::setw(width) << ss.str();
		ss.str(std::string());

		// print interloop deps
        for (auto i : it->interloop) {
            ss << i << " ";
        }
		ss << '|';
		std::cout << std::setfill(' ') << std::setw(width) << ss.str();
		ss.str(std::string());

		// print loop_invariant deps
		for (auto i : it->loop_invariant) {
            ss << i << " ";
        }
		ss << '|';
		std::cout << std::setfill(' ') << std::setw(width) << ss.str();
	    ss.str(std::string());

		// print post_loop deps
		for (auto i : it->post_loop) {
            ss << i << " ";
        }
		ss << '|';
		std::cout << std::setfill(' ') << std::setw(width) << ss.str();
		ss.str(std::string());

        std::cout << std::endl;
    }

    // Schedule instructions (main scheduling algorithm)
    auto time_table = schedule(dependencies);
    
    // Perform register allocation (allocb algorithm) - do this only ONCE
    auto [new_dest, new_use] = allocate_registers(dependencies, time_table);
    
    // Debug register allocation
    std::cout << "\nRegister allocation:\n";
    for (size_t i = 0; i < new_dest.size(); ++i) {
        if (new_dest[i] > 0) {
            std::cout << "Instruction " << i << ": x" << new_dest[i] << std::endl;
        }
    }

    // Helper function to apply renaming to an instruction
    auto apply_renaming = [&](int32_t id) -> Instruction {
        if (id < 0) return Instruction{Opcode::nop};
        
        Instruction instr = m_program[id]; // Make a copy
        
        if (instr.op == Opcode::nop || 
            instr.op == Opcode::loop || 
            instr.op == Opcode::loop_pip) {
            return instr; // No renaming needed for these
        }
        
        // Apply destination register renaming
        if (instr.op != Opcode::st && new_dest[id] > 0) {
            instr.dest = new_dest[id];
        } else if (instr.op == Opcode::st && new_dest[id] > 0) {
            // For store, dest is the data to store
            instr.dest = new_dest[id];
        }
        
        // Apply operand register renaming 
        auto [op_a, op_b] = new_use[id];
        
        // Apply op_a renaming for instructions that use it
        if (op_a != UINT32_MAX && 
           (instr.op == Opcode::add || instr.op == Opcode::sub || 
            instr.op == Opcode::mulu || instr.op == Opcode::addi || 
            instr.op == Opcode::ld || instr.op == Opcode::st || 
            instr.op == Opcode::movr)) {
            instr.op_a = op_a;
        }
        
        // Apply op_b renaming for instructions that use it
        if (op_b != UINT32_MAX && 
           (instr.op == Opcode::add || instr.op == Opcode::sub || 
            instr.op == Opcode::mulu)) {
            instr.op_b = op_b;
        }
        
        return instr;
    };

    // Create VLIWProgram from bundles
    VLIWProgram program;
    
    // For each bundle, create a VLIW instruction with all functional units
    for (const auto& bundle : m_bundles) {
        Instruction alu0 = Instruction{Opcode::nop};
        Instruction alu1 = Instruction{Opcode::nop};
        Instruction mult = Instruction{Opcode::nop};
        Instruction mem  = Instruction{Opcode::nop};
        Instruction br   = Instruction{Opcode::nop};

        for (int fu = 0; fu < 5; ++fu) {
            int instr_id = bundle[fu];
            if (instr_id < 0) continue;

            Instruction inst = apply_renaming(instr_id);
            // If it was a special LC/EC write, preserve that:
            if (m_program[instr_id].dest == lc_id) {
              inst.dest = lc_id;
            } else if (m_program[instr_id].dest == ec_id) {
              inst.dest = ec_id;
            }
            // Assign to correct slot
            switch (fu) {
                case 0: alu0 = inst; break;
                case 1: alu1 = inst; break;
                case 2: mult = inst; break;
                case 3: mem  = inst; break;
                case 4: br   = inst; break;
            }
        }

        program.alu0_instructions.push_back(alu0);
        program.alu1_instructions.push_back(alu1);
        program.mult_instructions.push_back(mult);
        program.mem_instructions.push_back(mem);
        program.branch_instructions.push_back(br);
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
        m_bundles.push_back({-1, -1, -1, -1, -1});
    }
    
    // Try each bundle starting from the lowest possible time
    for (uint64_t i_bundle = lowest_time; i_bundle < m_bundles.size(); ++i_bundle) {
        // Determine instruction type and check appropriate functional unit
        if (instr.op == Opcode::add || instr.op == Opcode::addi || 
            instr.op == Opcode::sub || instr.op == Opcode::movi || 
            instr.op == Opcode::movr || instr.op == Opcode::movp || 
            instr.op == Opcode::nop) {
            // ALU operations - check ALU0 then ALU1
            if (m_bundles[i_bundle][0] == -1) {
                // ALU0 is available
                m_bundles[i_bundle][0] = instr_id;
                time_table[instr_id] = i_bundle;
                return true;
            } else if (m_bundles[i_bundle][1] == -1) {
                // ALU1 is available
                m_bundles[i_bundle][1] = instr_id;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::mulu) {
            // Multiplication operation - check MUL unit
            if (m_bundles[i_bundle][2] == -1) {
                m_bundles[i_bundle][2] = instr_id;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
            // Memory operations - check MEM unit
            if (m_bundles[i_bundle][3] == -1) {
                m_bundles[i_bundle][3] = instr_id;
                time_table[instr_id] = i_bundle;
                return true;
            }
        } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
            // Branch operations - check BRANCH unit
            if (m_bundles[i_bundle][4] == -1) {
                m_bundles[i_bundle][4] = instr_id;
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
    
    uint64_t bundle_idx = m_bundles.size();
    m_bundles.emplace_back(Bundle{-1, -1, -1, -1, -1});
    
    // Determine which functional unit to use based on instruction type
    if (instr.op == Opcode::add || instr.op == Opcode::addi || 
        instr.op == Opcode::sub || instr.op == Opcode::movi || 
        instr.op == Opcode::movr || instr.op == Opcode::movp || 
        instr.op == Opcode::nop) {
        m_bundles[bundle_idx][0] = instr_id;  // ALU0
    } else if (instr.op == Opcode::mulu) {
        m_bundles[bundle_idx][2] = instr_id;  // MUL
    } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
        m_bundles[bundle_idx][3] = instr_id;  // MEM
    } else if (instr.op == Opcode::loop || instr.op == Opcode::loop_pip) {
        m_bundles[bundle_idx][4] = instr_id;  // BRANCH
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
    uint64_t loop_time = lowest_time_start_loop + time_need_after_loop;
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

/**
 * Perform register allocation (allocb algorithm)
 * Implements the register allocation described in section 3.3.1
 * Returns both destination and operand mappings in a single call
 */
std::pair<std::vector<uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> 
LoopCompiler::allocate_registers(const std::vector<Dependency>& dependencies, 
                                const std::vector<uint64_t>& time_table) const {
    size_t N = m_program.size();
    std::vector<uint32_t> new_dest(N, 0);
    std::vector<std::pair<uint32_t, uint32_t>> new_use(N, {UINT32_MAX, UINT32_MAX});
    uint32_t next_reg = 1;  // Start from register x1
    
    // Phase 1: Assign fresh destination registers in BUNDLE order (execution order)
    for (const auto& bundle : m_bundles) {
        for (int fu = 0; fu < 5; ++fu) {
            int32_t id = bundle[fu];
            if (id < 0) continue;  // Skip empty slots
            
            const auto& instr = m_program[id];
            // Skip instructions that don't produce values
            if (instr.op != Opcode::st && 
                instr.op != Opcode::loop && 
                instr.op != Opcode::loop_pip && 
                instr.op != Opcode::nop &&
                instr.op != Opcode::movp) {
                
                // Don't allocate new registers for LC or EC
                if (instr.dest == lc_id || instr.dest == ec_id) {
                    // Special registers keep their special IDs
                    new_dest[id] = instr.dest;
                } else {
                    // Normal registers get fresh allocations
                    new_dest[id] = next_reg++;
                }
            }
        }
    }
    
    // Phase 2: Link operands to producers using dependencies
    for (size_t i = 0; i < N; ++i) {
        const auto& deps = dependencies[i].local;
        const auto& instr = m_program[i];
        
        // Process each dependency for this instruction
        size_t dep_idx = 0;
        for (auto dep_id : deps) {
            uint32_t r = new_dest[dep_id];
            
            // Link different operands based on instruction type
            switch (instr.op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    // First dependency goes to op_a, second to op_b
                    if (dep_idx == 0) {
                        new_use[i].first = r;
                    } else if (dep_idx == 1) {
                        new_use[i].second = r;
                    }
                    break;
                    
                case Opcode::addi:
                case Opcode::ld:
                case Opcode::movr:
                    // These instructions only have one operand (op_a)
                    new_use[i].first = r;
                    break;
                    
                case Opcode::st:
                    // Store has two dependencies: data and address
                    if (dep_idx == 0) {
                        // First dependency is the data value (dest)
                        new_dest[i] = r;  // Store the value to store in dest 
                    } else if (dep_idx == 1) {
                        // Second dependency is the address base (op_a)
                        new_use[i].first = r;   // Store address base in op_a
                    }
                    break;
                    
                default:
                    break;
            }
            dep_idx++;
        }
    }
    
    // Phase 4: Fix undefined register reads
    // Process in BUNDLE order to match Python reference implementation
    for (const auto& bundle : m_bundles) {
        for (int fu = 0; fu < 5; ++fu) {
            int32_t id = bundle[fu];
            if (id < 0) continue;  // Skip empty slots
            
            const auto& instr = m_program[id];
            auto [op_a, op_b] = new_use[id];
            
            // Check if op_a is used by this instruction and is undefined (UINT32_MAX)
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || 
                 instr.op == Opcode::mulu || instr.op == Opcode::addi || 
                 instr.op == Opcode::ld) && 
                op_a == UINT32_MAX) {
                op_a = next_reg++;
            }
            
            // Special case for store address (op_a)
            // For store instructions, only rename the address register if it has already
            // been linked to a producer in Phase 2. If it's still UINT32_MAX, that means
            // it's a live-in value with no producer and should use the original register.
            if (instr.op == Opcode::st && op_a == UINT32_MAX) {
                // For live-in values, use the original register number
                op_a = instr.op_a;
            }
            
            // Check if op_b is used by this instruction and is undefined (UINT32_MAX)
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || 
                 instr.op == Opcode::mulu) && op_b == UINT32_MAX) {
                op_b = next_reg++;
            }
            
            new_use[id] = {op_a, op_b};
        }
    }
    
    return {new_dest, new_use};
}
