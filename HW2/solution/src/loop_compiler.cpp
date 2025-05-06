#include "loop_compiler.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

    // Uncomment to use alternative renaming algorithm
    rename(basic_blocks, dependencies, time_table);

    // Create VLIWProgram from bundles
    VLIWProgram program;

    // For each bundle, create a VLIW instruction with all functional units
    for (const auto bundle : m_bundles) {
        program.alu0_instructions.push_back(bundle[0] != -1 ?   m_program.at(bundle[0]) : Instruction{Opcode::nop});
        program.alu1_instructions.push_back(bundle[1] != -1 ?   m_program.at(bundle[1]) : Instruction{Opcode::nop});
        program.mult_instructions.push_back(bundle[2] != -1 ?   m_program.at(bundle[2]) : Instruction{Opcode::nop});
        program.mem_instructions.push_back(bundle[3] != -1 ?    m_program.at(bundle[3]) : Instruction{Opcode::nop});
        program.branch_instructions.push_back(bundle[4] != -1 ? m_program.at(bundle[4]) : Instruction{Opcode::nop});
    }

    return program;

    /*
    // Perform register allocation (allocb algorithm) - do this only ONCE
    auto [new_dest, new_use] = allocate_registers(dependencies, time_table);
    
    // For storing interloop dependencies that need mov instructions
    // We need this for the apply_renaming lambda
    std::vector<std::pair<uint64_t, uint64_t>> need_mov_phase3;
    
    // Process each instruction to find interloop dependencies that need movs
    // This is a simplified version of what we do in allocate_registers
    for (size_t i = 0; i < m_program.size(); ++i) {
        // Check if this instruction is in BB1 (loop body)
        bool inBB1 = (basic_blocks.size() > 1 && 
                     i >= basic_blocks[1].first && 
                     i < basic_blocks[1].second);
                     
        if (inBB1) {
            for (auto dep_id : dependencies[i].interloop) {
                // Check if dependency is from BB0 (before the loop)
                bool depFromBB0 = (dep_id < basic_blocks[1].first);
                
                if (depFromBB0) {
                    // Find a matching instruction in BB1 that produces a value
                    for (uint64_t bb1_id = basic_blocks[1].first; bb1_id < basic_blocks[1].second; ++bb1_id) {
                        if (bb1_id != i && new_dest[bb1_id] > 0) {
                            for (auto bb1_dep : dependencies[bb1_id].interloop) {
                                if (bb1_dep == dep_id) {
                                    // Add it to our mov phase3 list
                                    need_mov_phase3.push_back({dep_id, bb1_id});
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
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
        
        // Handle special case for mov instructions added during register allocation
        // These have IDs beyond the original program size
        if (id >= static_cast<int32_t>(m_program.size())) {
            // This is a mov instruction added for interloop dependencies
            Instruction mov_instr{Opcode::movr};
            
            // Determine the bundle index for this instruction
            uint64_t bundle_idx = 0;
            for (uint64_t i = 0; i < m_bundles.size(); ++i) {
                for (int fu = 0; fu < 5; ++fu) {
                    if (m_bundles[i][fu] == id) {
                        bundle_idx = i;
                        break;
                    }
                }
            }
            
            // Handle special case for the mov instructions we added
            // In this case, we have to recover the registers from the bundle information
            for (uint64_t i = 0; i < m_bundles.size(); ++i) {
                if (i == bundle_idx) {
                    for (int fu = 0; fu < 5; ++fu) {
                        if (m_bundles[i][fu] == id) {
                            // Found the mov instruction in a bundle
                            // We need to find what registers it's supposed to use
                            // For mov instructions added during register allocation, 
                            // we stored the destination register in new_dest (for BB0) and
                            // the source register comes from a BB1 instruction
                            
                            // We should be able to find both registers in our dependency tracking
                            for (const auto& [BB0_idx, BB1_idx] : need_mov_phase3) {
                                // If we find matching registers, create the mov instruction
                                if (new_dest[BB0_idx] > 0 && new_dest[BB1_idx] > 0) {
                                    mov_instr.dest = new_dest[BB0_idx];  // BB0 register
                                    mov_instr.op_a = new_dest[BB1_idx];  // BB1 register
                                    return mov_instr;
                                }
                            }
                        }
                    }
                }
            }
            
            // If we didn't find specific register info, create a generic mov
            mov_instr.dest = 1;  // Default to register x1
            mov_instr.op_a = 2;  // Default to register x2
            return mov_instr;
        }
        
        // Normal case - instruction from the original program
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
    */
}

/**
 * Main scheduling function - orchestrates the scheduling of all basic blocks
 */
std::vector<uint64_t> LoopCompiler::schedule(std::vector<Dependency>& dependencies) {
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


    std::cout << "m_loop_start_time: " << m_time_start_of_loop << std::endl;
    std::cout << "m_loop_end_time: " << m_time_end_of_loop << std::endl;
    // Create the final VLIW program
    VLIWProgram program;

    // For each bundle, create a VLIW instruction with all functional units
    for (const auto bundle : m_bundles) {
        program.alu0_instructions.push_back(bundle[0] != -1 ?   m_program.at(bundle[0]) : Instruction{Opcode::nop});
        program.alu1_instructions.push_back(bundle[1] != -1 ?   m_program.at(bundle[1]) : Instruction{Opcode::nop});
        program.mult_instructions.push_back(bundle[2] != -1 ?   m_program.at(bundle[2]) : Instruction{Opcode::nop});
        program.mem_instructions.push_back(bundle[3] != -1 ?    m_program.at(bundle[3]) : Instruction{Opcode::nop});
        program.branch_instructions.push_back(bundle[4] != -1 ? m_program.at(bundle[4]) : Instruction{Opcode::nop});
    }

    program.print();
    std::cout << std::endl;


    return time_table;
}

/**
 * Attempts to find the earliest possible bundle position for an instruction
 * Respects functional unit constraints (each bundle has limited units)
 */
bool LoopCompiler::insert_ASAP(uint64_t instr_id, uint64_t lowest_time, 
                              std::vector<uint64_t>& time_table) {
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
                        std::vector<uint64_t>& time_table) {
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
std::vector<uint64_t> LoopCompiler::schedule_bb0(std::vector<uint64_t>& time_table) {
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
std::vector<uint64_t> LoopCompiler::schedule_bb1(std::vector<uint64_t>& time_table) {
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // If BB1 is empty, set markers and handle the loop instruction separately
    if (basic_blocks[1].first >= basic_blocks[1].second - 1) {
        m_time_start_of_loop = m_bundles.size();
        
        // For empty loops, we still need to schedule the loop instruction
        uint64_t loop_ins_idx = basic_blocks[1].second - 1;
        
        // Store loop start address in the instruction (where to jump back to)
        Instruction& loop_instr = const_cast<Instruction&>(m_program[loop_ins_idx]);
        loop_instr.imm = m_bundles.size();
        
        // Create a separate bundle for the loop instruction
        append(loop_ins_idx, m_bundles.size(), time_table);
        
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
    
    // Store loop start address in the instruction (where to jump back to)
    Instruction& loop_instr = const_cast<Instruction&>(m_program[loop_ins_idx]);
    loop_instr.imm = lowest_time_start_loop;
    
    // First find the latest scheduled instruction in the loop body
    uint64_t latest_instr_time = 0;
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
        if (time_table[i] != UINT64_MAX) {
            // Account for instruction latency
            uint64_t latency = (m_program[i].op == Opcode::mulu) ? 3 : 1;
            latest_instr_time = std::max(latest_instr_time, time_table[i] + latency - 1);
        }
    }
    
    // Important: For the loop to work correctly, the loop instruction must come
    // after all other instructions in the loop body
    
    // Find the latest bundle that contains any loop body instruction
    uint64_t last_bundle_idx = 0;
    for (uint64_t i = basic_blocks[1].first; i < basic_blocks[1].second - 1; ++i) {
        if (time_table[i] != UINT64_MAX) {
            last_bundle_idx = std::max(last_bundle_idx, time_table[i]);
        }
    }
    
    // If there's an empty branch slot in the last bundle, use it
    // Otherwise, create a new bundle for the loop instruction
    if (latest_instr_time <= last_bundle_idx && last_bundle_idx < m_bundles.size() && m_bundles[last_bundle_idx][4] == -1) {
        m_bundles[last_bundle_idx][4] = loop_ins_idx;
        time_table[loop_ins_idx] = last_bundle_idx;
    } else {
        // Create a new bundle just for the loop instruction
        insert_ASAP(loop_ins_idx, latest_instr_time, time_table);
    }
    
    // Set loop end time
    m_time_end_of_loop = m_bundles.size();
    
    return time_table;
}

/**
 * Schedule basic block 2 (post-loop instructions)
 */
std::vector<uint64_t> LoopCompiler::schedule_bb2(std::vector<uint64_t>& time_table) {
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

void LoopCompiler::rename_consumer_operands(
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

void LoopCompiler::insert_mov_end_of_loop(
    const uint32_t instr_id,
    const uint64_t lowest_time
) {
    std::cout << "Inserting: " << m_program.at(instr_id).to_string() << " from " << lowest_time << std::endl;
    const auto loop_instr_id = m_bundles.at(m_time_end_of_loop - 1).at(4);

    // move the loop instruction enough so that we can schedule at lowest_time
    for (; m_time_end_of_loop - 1 < lowest_time; ++m_time_end_of_loop) {
        m_bundles.at(m_time_end_of_loop - 1).at(4) = -1;
        m_bundles.insert(m_bundles.begin() + m_time_end_of_loop, Bundle{-1, -1, -1, -1, -1});
        m_bundles.at(m_time_end_of_loop).at(4) = loop_instr_id;
    }

    assert(m_time_end_of_loop - 1 >= lowest_time);

    // make sure we have enough bundles
    for (auto cur_time {lowest_time}; ; ++cur_time) {
        if (m_bundles.at(cur_time).at(0) == -1) {
            m_bundles.at(cur_time).at(0) = instr_id;
            return;
        }
        if (m_bundles.at(cur_time).at(1) == -1) {
            m_bundles.at(cur_time).at(1) = instr_id;
            return;
        }
        // if both slots are full, and we are at the end of the loop
        if (cur_time == m_time_end_of_loop - 1) {
            // we are trying to schedule something at the end of the loop, so
            // we need to move the loop instruction to the new bundle
            m_bundles.insert(m_bundles.begin() + m_time_end_of_loop, Bundle{-1, -1, -1, -1, -1});
            m_bundles.at(cur_time).at(4) = -1;
            m_bundles.at(cur_time + 1).at(4) = loop_instr_id;
            ++m_time_end_of_loop;
        }
    }
}

void LoopCompiler::rename(
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies,
    const std::vector<uint64_t>& time_table
) {
    // phase 1
    for (auto& bundle : m_bundles) {
        for (const auto instr_id : bundle) {
            if (instr_id != -1) {
                auto& instr = m_program.at(instr_id);
                if (is_producer(instr.op) && instr.dest != lc_id && instr.dest != ec_id) {
                    instr.new_dest = m_next_non_rotating_reg++;
                }
            }
        }
    }

    // phase 2
    for (auto instr_id {0}; instr_id < m_program.size(); ++instr_id) {
        auto& instr = m_program.at(instr_id);
        auto& dependency = dependencies.at(instr_id);

        for (const auto dep : dependency.local) {
            const auto& instr_producer = m_program.at(dep);
            rename_consumer_operands(
                instr_producer.dest,
                instr_producer.new_dest,
                instr
            );
        }

        for (const auto dep : dependency.post_loop) {
            const auto& instr_producer = m_program.at(dep);
            rename_consumer_operands(
                instr_producer.dest,
                instr_producer.new_dest,
                instr
            );
        }

        for (const auto dep : dependency.loop_invariant) {
            const auto& instr_producer = m_program.at(dep);
            rename_consumer_operands(
                instr_producer.dest,
                instr_producer.new_dest,
                instr
            );
        }
    }

    // first is dst, second is src
    std::vector<std::pair<uint64_t, uint64_t>> additional_mov_instr;
    if (basic_blocks.size() > 1) {
        const auto& bb1 = basic_blocks.at(1);

        // fix interloop dependencies
        for (auto instr_id {0}; instr_id < m_program.size(); ++instr_id) {
            auto& instr = m_program.at(instr_id);
            auto& dependency = dependencies.at(instr_id);

            for (auto dep : dependency.interloop) {
                if (bb1.first <= dep && dep < bb1.second) {
                    auto& bb1_producer = m_program.at(dep);

                    // see if this dependency has another producer in BB0
                    const auto dep_bb0 = std::find_if(dependency.interloop.begin(), dependency.interloop.end(), [&](const auto& d) {
                        const auto& bb0_producer {m_program.at(d)};
                        return d < bb1.first && bb0_producer.dest == bb1_producer.dest;
                    });

                    if (dep_bb0 != dependency.interloop.end()) {
                        // there is a bb0 producer
                        auto& bb0_producer = m_program.at(*dep_bb0);
                        rename_consumer_operands(
                            bb0_producer.dest,
                            bb0_producer.new_dest,
                            instr
                        );
                        // only append the mov instruction if we need to
                        if (std::find(additional_mov_instr.begin(), additional_mov_instr.end(), std::make_pair(*dep_bb0, dep)) == additional_mov_instr.end()) {
                            additional_mov_instr.emplace_back(*dep_bb0, dep);
                        }
                    } else {
                        // there is only a bb1 producer
                        rename_consumer_operands(
                            bb1_producer.dest,
                            bb1_producer.new_dest,
                            instr
                        );
                    }
                }
            }
        }
    }

    // phase 3
    const auto time_end_of_loop_before_phase_3 {m_time_end_of_loop};
    for (auto& p : additional_mov_instr) {
        const auto bb0_producer_id = p.first;
        const auto bb1_producer_id = p.second;
        m_program.push_back(Instruction{
            .op = Opcode::movr,
            .dest = static_cast<uint32_t>(m_program.at(bb0_producer_id).new_dest),
            .op_a = static_cast<uint32_t>(m_program.at(bb1_producer_id).new_dest),
            // to prevent this operand from being renamed
            .has_op_a_been_renamed = true,
        });

        const auto cur_instr_id = m_program.size() - 1;

        const auto latency = m_program.at(bb1_producer_id).op == Opcode::mulu ? 3 : 1;
        // we should only schedule the mov instructions at or after the end of the loop scheduled in phase 2
        const auto lowest_time = std::max(time_end_of_loop_before_phase_3 - 1, time_table.at(bb1_producer_id) + latency);
        insert_mov_end_of_loop(cur_instr_id, lowest_time);
    }

    // phase 4
    for (auto& bundle : m_bundles) {
        for (const auto instr_id : bundle) {
            if (instr_id != -1) {
                auto& instr = m_program.at(instr_id);

                switch (instr.op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    if (!instr.has_op_a_been_renamed) {
                        instr.has_op_a_been_renamed = true;
                        instr.op_a = m_next_non_rotating_reg++;
                    }
                    if (!instr.has_op_b_been_renamed) {
                        instr.has_op_b_been_renamed = true;
                        instr.op_b = m_next_non_rotating_reg++;
                    }
                    break;
                case Opcode::addi:
                case Opcode::ld:
                case Opcode::movr:
                    if (!instr.has_op_a_been_renamed) {
                        instr.has_op_a_been_renamed = true;
                        instr.op_a = m_next_non_rotating_reg++;
                    }
                    break;
                case Opcode::st:
                    if (!instr.has_dest_been_renamed) {
                        instr.has_dest_been_renamed = true;
                        instr.dest = m_next_non_rotating_reg++;
                    }
                    if (!instr.has_op_a_been_renamed) {
                        instr.has_op_a_been_renamed = true;
                        instr.op_a = m_next_non_rotating_reg++;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
}

/**
 * Inserts a mov instruction at the end of the loop
 * Used for handling interloop dependencies
 */
void LoopCompiler::insert_mov_at_end_of_loop(uint32_t dest_reg, uint32_t src_reg, 
                                           std::vector<uint64_t>& time_table) {
    // Determine the index for the new instruction
    uint64_t instr_id = m_program.size();
    
    // Create a new mov instruction
    Instruction mov_instr{Opcode::movr};
    mov_instr.dest = dest_reg;
    mov_instr.op_a = src_reg;
    
    // Find the appropriate bundle to insert, right after the loop instruction
    uint64_t lowest_time = m_time_end_of_loop;
    
    // Try to insert into an existing bundle
    bool inserted = false;
    for (uint64_t i = lowest_time; i < m_bundles.size() && !inserted; ++i) {
        // Try to use ALU0 first, then ALU1
        if (m_bundles[i][0] == -1) {
            m_bundles[i][0] = instr_id;
            time_table[instr_id] = i;
            inserted = true;
        } else if (m_bundles[i][1] == -1) {
            m_bundles[i][1] = instr_id;
            time_table[instr_id] = i;
            inserted = true;
        }
    }
    
    // If we couldn't insert into an existing bundle, create a new one
    if (!inserted) {
        append(instr_id, lowest_time, time_table);
    }
    
    // Note: In C++, we can't actually add to m_program since it's the input program
    // Just setting time_table ensures this mov will be considered in the final output
    // The actual mov instruction will be created in the apply_renaming function
}

/**
 * Perform register allocation (allocb algorithm)
 * Implements the register allocation described in section 3.3.1
 * Returns both destination and operand mappings in a single call
 */
std::pair<std::vector<uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> 
LoopCompiler::allocate_registers(const std::vector<Dependency>& dependencies, 
                                const std::vector<uint64_t>& time_table) {
    size_t N = m_program.size();
    std::vector<uint32_t> new_dest(N, 0);
    std::vector<std::pair<uint32_t, uint32_t>> new_use(N, {UINT32_MAX, UINT32_MAX});
    uint32_t next_reg = 1;  // Start from register x1
    std::unordered_map<uint32_t, uint32_t> livein_pointer_renaming;
    
    // For storing interloop dependencies that need mov instructions
    std::vector<std::pair<uint64_t, uint64_t>> need_mov_phase3;
    
    // Track basic blocks
    auto basic_blocks = find_basic_blocks();

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
        const auto& instr = m_program[i];
        
        // Handle local dependencies
        const auto& local_deps = dependencies[i].local;
        size_t dep_idx = 0;
        for (auto dep_id : local_deps) {
            uint32_t r = new_dest[dep_id];
            
            // Handle the case where the same operand is used twice (like sub x3, x2, x2)
            // Check if this is a binary operation where both operands are the same register
            bool same_operands = false;
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                instr.op_a == instr.op_b) {
                same_operands = true;
            }
            
            // Link different operands based on instruction type
            switch (instr.op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    // First dependency goes to op_a
                    if (dep_idx == 0) {
                        new_use[i].first = r;
                        
                        // If both operands were the same in the original code,
                        // we need to use the same register for both operands
                        if (same_operands) {
                            new_use[i].second = r;
                        }
                    } 
                    // Second dependency goes to op_b (if not same_operands)
                    else if (dep_idx == 1 && !same_operands) {
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
        
        // Handle loop invariant dependencies
        const auto& loop_invariant_deps = dependencies[i].loop_invariant;
        for (auto dep_id : loop_invariant_deps) {
            uint32_t r = new_dest[dep_id];
            
            // Handle the case where the same operand is used twice
            bool same_operands = false;
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                instr.op_a == instr.op_b) {
                same_operands = true;
            }
            
            // Determine which operand to link based on instruction type
            switch (instr.op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    // First loop invariant dependency goes to op_a if not set already
                    if (new_use[i].first == UINT32_MAX) {
                        new_use[i].first = r;
                        
                        // If both operands were the same in the original code,
                        // we need to use the same register for both operands
                        if (same_operands && new_use[i].second == UINT32_MAX) {
                            new_use[i].second = r;
                        }
                    } 
                    // Second loop invariant dependency goes to op_b if not set already
                    else if (new_use[i].second == UINT32_MAX && !same_operands) {
                        new_use[i].second = r;
                    }
                    break;
                    
                case Opcode::addi:
                case Opcode::ld:
                case Opcode::movr:
                    // These instructions only have one operand (op_a)
                    if (new_use[i].first == UINT32_MAX) {
                        new_use[i].first = r;
                    }
                    break;
                    
                case Opcode::st:
                    // For store, the dependency could be either the data or address
                    if (new_dest[i] == 0) {
                        // First: assume it's the data value
                        new_dest[i] = r;
                    } else if (new_use[i].first == UINT32_MAX) {
                        // Second: if dest is already set, it must be the address base
                        new_use[i].first = r;
                    }
                    break;
                    
                default:
                    break;
            }
        }
        
        // Handle post-loop dependencies (similar to local dependencies)
        const auto& post_loop_deps = dependencies[i].post_loop;
        for (auto dep_id : post_loop_deps) {
            uint32_t r = new_dest[dep_id];
            
            // Handle the case where the same operand is used twice
            bool same_operands = false;
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                instr.op_a == instr.op_b) {
                same_operands = true;
            }
            
            // Similar logic to local dependencies
            switch (instr.op) {
                case Opcode::add:
                case Opcode::sub:
                case Opcode::mulu:
                    if (new_use[i].first == UINT32_MAX) {
                        new_use[i].first = r;
                        
                        // If both operands were the same in the original code,
                        // we need to use the same register for both operands
                        if (same_operands && new_use[i].second == UINT32_MAX) {
                            new_use[i].second = r;
                        }
                    } else if (new_use[i].second == UINT32_MAX && !same_operands) {
                        new_use[i].second = r;
                    }
                    break;
                    
                case Opcode::addi:
                case Opcode::ld:
                case Opcode::movr:
                    if (new_use[i].first == UINT32_MAX) {
                        new_use[i].first = r;
                    }
                    break;
                    
                case Opcode::st:
                    if (new_dest[i] == 0) {
                        new_dest[i] = r;
                    } else if (new_use[i].first == UINT32_MAX) {
                        new_use[i].first = r;
                    }
                    break;
                    
                default:
                    break;
            }
        }
        
        // Handle interloop dependencies - this is the key part of Phase 3
        const auto& interloop_deps = dependencies[i].interloop;
        
        // Check if this instruction is in BB1 (loop body)
        bool inBB1 = (basic_blocks.size() > 1 && 
                     i >= basic_blocks[1].first && 
                     i < basic_blocks[1].second);
                     
        if (inBB1) {
            for (auto dep_id : interloop_deps) {
                // Check if dependency is from BB0 (before the loop)
                bool depFromBB0 = (dep_id < basic_blocks[1].first);
                
                if (depFromBB0) {
                    // For instructions in BB1 with dependencies from BB0,
                    // we'll need to add a mov at the end of the loop
                    
                    // First, find a matching instruction in BB1 that produces the same value
                    for (uint64_t bb1_id = basic_blocks[1].first; bb1_id < basic_blocks[1].second; ++bb1_id) {
                        if (bb1_id != i && new_dest[bb1_id] > 0) {
                            // This is a different instruction in BB1 that produces a value
                            
                            // If we find a matching instruction, register its need for a mov
                            // For simplicity, we'll assume if both instructions have dependencies
                            // on the same BB0 instruction, they might need a mov
                            for (auto bb1_dep : dependencies[bb1_id].interloop) {
                                if (bb1_dep == dep_id) {
                                    // This BB1 instruction depends on the same BB0 instruction
                                    // Add it to our mov phase3 list
                                    need_mov_phase3.push_back({dep_id, bb1_id});
                                    break;
                                }
                            }
                        }
                    }
                    
                    // Link operands based on instruction type
                    // Similar to local dependencies, but map to the BB0 register
                    uint32_t r = new_dest[dep_id];  // Use the BB0 register
                    
                    // Handle the case where the same operand is used twice
                    bool same_operands = false;
                    if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                        instr.op_a == instr.op_b) {
                        same_operands = true;
                    }
                    
                    switch (instr.op) {
                        case Opcode::add:
                        case Opcode::sub:
                        case Opcode::mulu:
                            if (new_use[i].first == UINT32_MAX) {
                                new_use[i].first = r;
                                
                                // If both operands were the same in the original code,
                                // we need to use the same register for both operands
                                if (same_operands && new_use[i].second == UINT32_MAX) {
                                    new_use[i].second = r;
                                }
                            } else if (new_use[i].second == UINT32_MAX && !same_operands) {
                                new_use[i].second = r;
                            }
                            break;
                            
                        case Opcode::addi:
                        case Opcode::ld:
                        case Opcode::movr:
                            if (new_use[i].first == UINT32_MAX) {
                                new_use[i].first = r;
                            }
                            break;
                            
                        case Opcode::st:
                            if (new_dest[i] == 0) {
                                new_dest[i] = r;
                            } else if (new_use[i].first == UINT32_MAX) {
                                new_use[i].first = r;
                            }
                            break;
                            
                        default:
                            break;
                    }
                }
            }
        }
    }

    // Phase 3: Add mov instructions for interloop dependencies
    // For each interloop dependency, add a mov instruction at the end of the loop
    // In Python, this is tracked in the need_mov_phase3 set
    if (!need_mov_phase3.empty()) {
        // We need to expand the time_table to account for new mov instructions
        auto mutable_time_table = time_table;
        mutable_time_table.resize(N + need_mov_phase3.size(), UINT64_MAX);
        
        for (const auto& [BB0_id, BB1_id] : need_mov_phase3) {
            uint32_t BB0_reg = new_dest[BB0_id];
            uint32_t BB1_reg = new_dest[BB1_id];
            
            if (BB0_reg > 0 && BB1_reg > 0) {
                // Calculate the lowest time to insert the mov
                uint64_t lowest_time = m_time_end_of_loop;
                uint64_t ins_latency = (m_program[BB1_id].op == Opcode::mulu) ? 3 : 1;
                lowest_time = std::max(lowest_time, time_table[BB1_id] + ins_latency);
                
                // Create and insert the mov instruction
                insert_mov_at_end_of_loop(BB0_reg, BB1_reg, mutable_time_table);
            }
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
            
            if ((instr.op == Opcode::ld || instr.op == Opcode::st) && op_a == UINT32_MAX) {
                bool has_dep = !dependencies[id].local.empty() || !dependencies[id].loop_invariant.empty()
                            || !dependencies[id].post_loop.empty() || !dependencies[id].interloop.empty();

                if (has_dep) {
                    op_a = next_reg++;  // Rename only if there's a dependency on the pointer
                } else {
                    op_a = instr.op_a;  // Keep the original pointer register
                }
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
