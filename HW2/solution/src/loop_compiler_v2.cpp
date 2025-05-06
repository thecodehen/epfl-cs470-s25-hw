#include "loop_compiler.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <unordered_map>

/**
 * Main compilation method that orchestrates the loop scheduling process
 * and returns the final VLIW program
 */
VLIWProgram LoopCompiler::compile() {
    // Find basic blocks and analyze dependencies
    auto basic_blocks = find_basic_blocks();
    auto dependencies = find_dependencies(basic_blocks);
    
    // Log basic blocks for debugging
    for (const auto& block : basic_blocks) {
        std::cout << "Basic block: " << block.first << " to " << block.second << std::endl;
    }
    
    // Calculate the minimum initiation interval
    uint32_t min_ii = compute_min_initiation_interval();
    std::cout << "min II = " << min_ii << std::endl;
    
    // Schedule instructions
    auto time_table = schedule(dependencies, basic_blocks);
    
    // Perform register allocation 
    auto [new_dest, new_use] = allocate_registers(dependencies, time_table, basic_blocks);
    
    // Use member variable that tracks original program size
    auto original_size = m_orig_program_size;
    
    // Debug register allocation
    std::cout << "\nRegister allocation:\n";
    for (size_t i = 0; i < new_dest.size(); ++i) {
        if (new_dest[i] > 0) {
            std::cout << "Instruction " << i << ": x" << new_dest[i] << std::endl;
        }
    }
    
    // Debug mov instructions
    std::cout << "\nMov instructions added (" << m_program.size() - original_size << "):\n";
    for (size_t i = original_size; i < m_program.size(); ++i) {
        const auto& instr = m_program[i];
        std::cout << "Mov instr " << i << ": " << instr.to_string()
                  << " (dest=" << new_dest[i] << ", op_a=" << new_use[i].first << ")" << std::endl;
    }
    
    // Debug bundles
    std::cout << "\nBundle contents:\n";
    for (size_t i = 0; i < m_bundles.size(); ++i) {
        std::cout << "Bundle " << i << ": ";
        for (int j = 0; j < 5; ++j) {
            std::cout << m_bundles[i][j] << " ";
        }
        std::cout << std::endl;
    }

    // Create VLIW program from scheduled bundles
    VLIWProgram program;
    // Utility function to apply register renaming to an instruction
    auto apply_renaming = [&](int32_t id) -> Instruction {
      if (id < 0) return Instruction{Opcode::nop};

      // If the instruction ID is beyond the original program size,
      // it's one of our movr instructions
      if (id >= static_cast<int32_t>(original_size)) {
          if (id < static_cast<int32_t>(new_dest.size())) {
              // This is a movr instruction we added - return it with register renaming
              Instruction instr = m_program[id]; // Make a copy
              // Apply proper register renaming
              instr.dest = new_dest[id];
              instr.op_a = new_use[id].first;
              // Make sure we use the proper register format
              return instr;
          } else {
              // This shouldn't happen, but just in case
              return m_program[id];
          }
      }

      // Regular instructions continue with normal processing
      Instruction instr = m_program[id]; // Make a copy

        // Skip special instructions
        if (instr.op == Opcode::nop || 
            instr.op == Opcode::loop) {
            return instr;
        }
        
        // Apply destination register renaming (except for special registers)
        if (instr.dest != lc_id && instr.dest != ec_id) {
            if (instr.op != Opcode::st) {
                instr.dest = new_dest[id];
            } else {
                // For store, dest is the data to store
                instr.dest = new_dest[id];
            }
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

    // Build final program by applying renaming to all instructions in bundles
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
            // Preserve special register mappings
            if (instr_id < static_cast<int32_t>(original_size)) {
                if (m_program[instr_id].dest == lc_id) {
                    inst.dest = lc_id;
                } else if (m_program[instr_id].dest == ec_id) {
                    inst.dest = ec_id;
                }
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
std::vector<uint64_t> LoopCompiler::schedule(
    std::vector<Dependency>& dependencies,
    const std::vector<Block>& basic_blocks) 
{
    // Initialize time table to track instruction scheduling
    std::vector<uint64_t> time_table(m_program.size(), UINT64_MAX);
    
    // Reset bundles
    m_bundles.clear();
    
    // Schedule each basic block in order
    schedule_bb0(time_table, basic_blocks, dependencies);
    
    // If there's a loop, schedule it
    if (basic_blocks.size() > 1) {
        m_time_start_of_loop = m_bundles.size(); // Mark start of loop
        schedule_bb1(time_table, basic_blocks, dependencies);
        m_time_end_of_loop = m_bundles.size(); // Mark end of loop
        
        // If there's post-loop code, schedule it
        if (basic_blocks.size() > 2) {
            schedule_bb2(time_table, basic_blocks, dependencies);
        }
    }
    
    return time_table;
}

/**
 * Schedule basic block 0 (pre-loop instructions) with ASAP algorithm
 */
void LoopCompiler::schedule_bb0(
    std::vector<uint64_t>& time_table,
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies) 
{
    const Block& bb0 = basic_blocks[0];
    
    // Process each instruction in BB0
    for (uint64_t i = bb0.first; i < bb0.second; ++i) {
        uint64_t lowest_time = 0;
        
        // Calculate the earliest possible time based on local dependencies
        for (uint64_t dep_id : dependencies[i].local) {
            // Respect instruction latency (3 cycles for MUL, 1 for others)
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
        }
        
        // Schedule the instruction ASAP
        if (!schedule_instruction_asap(i, lowest_time, time_table)) {
            append_instruction(i, lowest_time, time_table);
        }
    }
}

/**
 * Schedule basic block 1 (loop body instructions) with ASAP algorithm
 * respecting loop dependencies
 */
void LoopCompiler::schedule_bb1(
    std::vector<uint64_t>& time_table,
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies) 
{
    const Block& bb1 = basic_blocks[1];
    
    // If BB1 is empty, just mark loop boundaries and return
    if (bb1.first >= bb1.second) {
        m_time_start_of_loop = m_bundles.size();
        m_time_end_of_loop = m_bundles.size();
        return;
    }
    
    // Calculate earliest start time for loop based on dependencies from BB0
    uint64_t lowest_start_time = m_bundles.size();
    
    // Consider loop invariant and interloop dependencies
    for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
        // Check loop invariant dependencies
        for (uint64_t dep_id : dependencies[i].loop_invariant) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            lowest_start_time = std::max(lowest_start_time, time_table[dep_id] + latency);
        }
        
        // Check interloop dependencies from BB0
        for (uint64_t dep_id : dependencies[i].interloop) {
            if (dep_id < bb1.first) { // Only consider dependencies from BB0
                uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
                lowest_start_time = std::max(lowest_start_time, time_table[dep_id] + latency);
            }
        }
    }
    
    // Schedule all instructions except the last one (which is the loop instruction)
    for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
        uint64_t lowest_time = lowest_start_time;
        
        // Consider local dependencies within the loop
        for (uint64_t dep_id : dependencies[i].local) {
            uint64_t latency = (m_program[dep_id].op == Opcode::mulu) ? 3 : 1;
            if (time_table[dep_id] != UINT64_MAX) { // Only if dependency is already scheduled
                lowest_time = std::max(lowest_time, time_table[dep_id] + latency);
            }
        }
        
        // Schedule the instruction
        if (!schedule_instruction_asap(i, lowest_time, time_table)) {
            append_instruction(i, lowest_time, time_table);
        }
    }
    
    // Handle the last instruction (loop)
    uint64_t loop_instr_id = bb1.second - 1;
    
    // Set the loop instruction's target to the start of the loop
    Instruction& loop_instr = const_cast<Instruction&>(m_program[loop_instr_id]);
    loop_instr.imm = lowest_start_time;
    
    // Find the latest scheduled instruction in the loop body
    uint64_t latest_time = 0;
    for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
        if (time_table[i] != UINT64_MAX) {
            uint64_t latency = (m_program[i].op == Opcode::mulu) ? 3 : 1;
            latest_time = std::max(latest_time, time_table[i] + latency - 1);
        }
    }
    
    // Place the loop instruction at the end of the loop
    uint64_t latest_bundle = 0;
    for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
        if (time_table[i] != UINT64_MAX) {
            latest_bundle = std::max(latest_bundle, time_table[i]);
        }
    }
    
    // If there's space in the last bundle, use it; otherwise, create a new one
    if (latest_bundle < m_bundles.size() && m_bundles[latest_bundle][4] == -1) {
        m_bundles[latest_bundle][4] = loop_instr_id;
        time_table[loop_instr_id] = latest_bundle;
    } else {
        append_instruction(loop_instr_id, latest_time, time_table);
    }
}

/**
 * Schedule basic block 2 (post-loop instructions) with ASAP algorithm
 * respecting dependencies from previous blocks
 */
void LoopCompiler::schedule_bb2(
    std::vector<uint64_t>& time_table,
    const std::vector<Block>& basic_blocks,
    const std::vector<Dependency>& dependencies) 
{
    const Block& bb2 = basic_blocks[2];
    
    // Process each instruction in BB2
    for (uint64_t i = bb2.first; i < bb2.second; ++i) {
        // Start from current end of schedule
        uint64_t lowest_time = m_bundles.size();
        
        // Consider all dependencies from previous code
        
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
        
        // Schedule the instruction
        if (!schedule_instruction_asap(i, lowest_time, time_table)) {
            append_instruction(i, lowest_time, time_table);
        }
    }
}

/**
 * Attempt to schedule an instruction ASAP (As Soon As Possible)
 * in a bundle that has the required functional unit available
 */
bool LoopCompiler::schedule_instruction_asap(
    uint64_t instr_id,
    uint64_t lowest_time,
    std::vector<uint64_t>& time_table) 
{
    const auto& instr = m_program[instr_id];
    
    // Make sure we have enough bundles
    while (m_bundles.size() <= lowest_time) {
        m_bundles.push_back({-1, -1, -1, -1, -1});
    }
    
    // Try each bundle starting from the earliest possible time
    for (uint64_t time = lowest_time; time < m_bundles.size(); ++time) {
        // Find appropriate functional unit based on instruction type
        int fu_index = -1;
        
        if (instr.op == Opcode::add || instr.op == Opcode::addi || 
            instr.op == Opcode::sub || instr.op == Opcode::movi || 
            instr.op == Opcode::movr || instr.op == Opcode::movp || 
            instr.op == Opcode::nop) {
            // ALU operations - try ALU0 then ALU1
            if (m_bundles[time][0] == -1) {
                fu_index = 0;
            } else if (m_bundles[time][1] == -1) {
                fu_index = 1;
            }
        } else if (instr.op == Opcode::mulu) {
            // Multiplication operation - try MUL unit
            if (m_bundles[time][2] == -1) {
                fu_index = 2;
            }
        } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
            // Memory operations - try MEM unit
            if (m_bundles[time][3] == -1) {
                fu_index = 3;
            }
        } else if (instr.op == Opcode::loop) {
            // Branch operations - try BRANCH unit
            if (m_bundles[time][4] == -1) {
                fu_index = 4;
            }
        }
        
        // If we found an available unit, schedule the instruction
        if (fu_index != -1) {
            m_bundles[time][fu_index] = instr_id;
            time_table[instr_id] = time;
            return true;
        }
    }
    
    // No suitable slot found
    return false;
}

/**
 * Create a new bundle for an instruction when ASAP scheduling fails
 */
void LoopCompiler::append_instruction(
    uint64_t instr_id,
    uint64_t lowest_time,
    std::vector<uint64_t>& time_table) 
{
    const auto& instr = m_program[instr_id];
    
    // Create a new bundle
    m_bundles.push_back({-1, -1, -1, -1, -1});
    uint64_t bundle_idx = m_bundles.size() - 1;
    
    // Select appropriate functional unit based on instruction type
    int fu_index = -1;
    
    if (instr.op == Opcode::add || instr.op == Opcode::addi || 
        instr.op == Opcode::sub || instr.op == Opcode::movi || 
        instr.op == Opcode::movr || instr.op == Opcode::movp || 
        instr.op == Opcode::nop) {
        fu_index = 0;  // ALU0
    } else if (instr.op == Opcode::mulu) {
        fu_index = 2;  // MUL
    } else if (instr.op == Opcode::ld || instr.op == Opcode::st) {
        fu_index = 3;  // MEM
    } else if (instr.op == Opcode::loop) {
        fu_index = 4;  // BRANCH
    }
    
    // Schedule the instruction in the new bundle
    if (fu_index != -1) {
        m_bundles[bundle_idx][fu_index] = instr_id;
        time_table[instr_id] = bundle_idx;
    }
}

void LoopCompiler::insert_mov_end_of_loop(
      const uint32_t instr_id,
      const uint64_t lowest_time,
      std::vector<uint64_t>& time_table  // Add time_table parameter
  ) {
      const auto loop_instr_id = m_bundles.at(m_time_end_of_loop - 1).at(4);
      auto cur_time = m_time_end_of_loop - 1;

      while (cur_time < lowest_time) {
          m_bundles.at(cur_time).at(4) = -1;
          m_bundles.insert(m_bundles.begin() + cur_time + 1, Bundle{-1, -1, -1, -1, -1});
          m_bundles.at(cur_time + 1).at(4) = loop_instr_id;
          ++cur_time;
      }

      // make sure we have enough bundles
      while (true) {
          if (m_bundles.at(cur_time).at(0) == -1) {
              m_bundles.at(cur_time).at(0) = instr_id;
              if (instr_id >= time_table.size()) {
                  time_table.resize(m_program.size(), UINT64_MAX); // Make sure time_table is big enough
              }
              time_table[instr_id] = cur_time; // Update time_table with this instruction's position
              return;
          }
          if (m_bundles.at(cur_time).at(1) == -1) {
              m_bundles.at(cur_time).at(1) = instr_id;
              if (instr_id >= time_table.size()) {
                  time_table.resize(m_program.size(), UINT64_MAX); // Make sure time_table is big enough
              }
              time_table[instr_id] = cur_time; // Update time_table with this instruction's position
              return;
          }
          m_bundles.at(cur_time).at(4) = -1;
          m_bundles.insert(m_bundles.begin() + cur_time + 1, Bundle{-1, -1, -1, -1, -1});
          m_bundles.at(cur_time + 1).at(4) = loop_instr_id;
          ++cur_time;
      }
  }
/**
 * Perform register allocation using a simplified algorithm:
 * 1. Allocate destination registers
 * 2. Link operands to their producer registers
 * 3. Fix interloop dependencies
 * 4. Assign registers to undefined operands
 */
std::pair<std::vector<uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> 
LoopCompiler::allocate_registers(
    const std::vector<Dependency>& dependencies, 
    const std::vector<uint64_t>& time_table,
    const std::vector<Block>& basic_blocks) 
{
    // Print instructions and their dependencies for debugging
      std::cout << "\n=== DEPENDENCY DEBUG FOR TEST 09 ===\n";
      for (size_t i = 0; i < m_program.size(); ++i) {
          const auto& instr = m_program[i];

          // Print basic instruction info
          std::cout << "Instr " << i << ": "
                    << instr.to_string()
                    << " (dest=" << instr.dest
                    << ", op_a=" << instr.op_a;

          if (instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) {
              std::cout << ", op_b=" << instr.op_b;
          }
          std::cout << ")\n";

          // Print dependencies
          std::cout << "  local deps: ";
          for (auto dep : dependencies[i].local) {
              std::cout << dep << " ";
          }
          std::cout << "\n";

          std::cout << "  loop_invariant deps: ";
          for (auto dep : dependencies[i].loop_invariant) {
              std::cout << dep << " ";
          }
          std::cout << "\n";

          std::cout << "  post_loop deps: ";
          for (auto dep : dependencies[i].post_loop) {
              std::cout << dep << " ";
          }
          std::cout << "\n";

          std::cout << "  interloop deps: ";
          for (auto dep : dependencies[i].interloop) {
              std::cout << dep << " ";
          }
          std::cout << "\n";
      }
      std::cout << "===============================\n\n";
    size_t N = m_program.size();
    std::vector<uint32_t> new_dest(N, 0);
    std::vector<std::pair<uint32_t, uint32_t>> new_use(N, {UINT32_MAX, UINT32_MAX});
    uint32_t next_reg = 1;  // Start from register x1
    std::vector<std::pair<uint64_t, uint64_t>> need_mov_phase3;

    // Phase 1: Assign destination registers in execution order
    for (const auto& bundle : m_bundles) {
        for (int fu = 0; fu < 5; ++fu) {
            int32_t id = bundle[fu];
            if (id < 0) continue;  // Skip empty slots
            
            const auto& instr = m_program[id];
            // Skip non-producing instructions
            if (instr.op != Opcode::st && 
                instr.op != Opcode::loop &&
                instr.op != Opcode::nop) {
                
                // Keep special register IDs
                if (instr.dest == lc_id || instr.dest == ec_id) {
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
        
        // Handle each dependency type
        auto process_deps = [&](const std::vector<uint64_t>& deps) {
            for (auto dep_id : deps) {
                uint32_t r = new_dest[dep_id];
                if (r == 0) continue;  // Skip if no value produced
                
                // Check if using same register for both operands
                bool same_operands = false;
                if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                    instr.op_a == instr.op_b) {
                    same_operands = true;
                }
                
                // Link operands based on instruction type
                switch (instr.op) {
                    case Opcode::add:
                    case Opcode::sub:
                    case Opcode::mulu:
                        if (new_use[i].first == UINT32_MAX) {
                            new_use[i].first = r;
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
                        std::cout << "ci siamo\n";
                        if (new_dest[i] == 0) {
                            new_dest[i] = r;  // Data to store
                            std::cout << r << "\n";
                        } else if (new_use[i].first == UINT32_MAX) {
                            new_use[i].first = r;  // Address
                            std::cout << r << "  elif \n";
                        }
                        break;
                        
                    default:
                        break;
                }
            }
        };
        
        // Process each type of dependency
        process_deps(dependencies[i].local);
        process_deps(dependencies[i].loop_invariant);
        process_deps(dependencies[i].post_loop);
        
        // Handle interloop dependencies specially
        if (basic_blocks.size() > 1) {
            const auto& bb1 = basic_blocks[1];
            bool in_loop = (i >= bb1.first && i < bb1.second);

            if (in_loop) {
                for (auto dep_id : dependencies[i].interloop) {
                    // If dependency is from outside the loop
                    if (dep_id < bb1.first) {
                        // Find a corresponding BB1 instruction that produces the same register
                        for (uint64_t bb1_id = bb1.first; bb1_id < bb1.second - 1; ++bb1_id) {
                            // If this is a different instruction in the loop
                            if (bb1_id != i &&
                                // Skip self
                                // And it produces a value (has a destination)
                                m_program[bb1_id].op != Opcode::st &&
                                m_program[bb1_id].op != Opcode::loop &&
                                m_program[bb1_id].op != Opcode::nop) {

                                // Check if it has an interloop dependency on the same BB0 instruction
                                for (auto bb1_dep : dependencies[bb1_id].interloop) {
                                    if (bb1_dep == dep_id) {
                                        // This is a case where we need a mov to maintain the value
                                        need_mov_phase3.push_back({dep_id, bb1_id});
                                        break;
                                    }
                                }
                            }
                        }

                        // Continue with your existing interloop register handling
                        uint32_t r = new_dest[dep_id];
                        if (r == 0) continue;
                        
                        // Same linking logic as above
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
                                std::cout << "ci siamo111\n";
                                if (new_dest[i] == 0) {
                                    new_dest[i] = r;
                                    std::cout << r << "\n";
                                } else if (new_use[i].first == UINT32_MAX) {
                                    new_use[i].first = r;
                                    std::cout << r << " elif\n";
                                }
                                break;
                                
                            default:
                                break;
                        }
                    }
                }
            }
        }
    }
    
    // Phase 4: Fix undefined register reads
    for (const auto& bundle : m_bundles) {
        for (int fu = 0; fu < 5; ++fu) {
            int32_t id = bundle[fu];
            if (id < 0) continue;  // Skip empty slots
            
            const auto& instr = m_program[id];
            auto [op_a, op_b] = new_use[id];
            
            if (instr.op == Opcode::st) {
                // For store instructions, ensure dest (data to store) is set
                if (new_dest[id] == 0 && op_a == UINT32_MAX) {
                    new_dest[id] = instr.dest; // Use original data register
                    op_a = instr.op_a; // Use original address register
                }else if(new_dest[id] != 0 && op_a == UINT32_MAX){
                    op_a = next_reg++;
                }
            }
            // Check if op_a is used but undefined
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || 
                instr.op == Opcode::mulu || instr.op == Opcode::addi || 
                instr.op == Opcode::ld) && 
                op_a == UINT32_MAX) {
                op_a = next_reg++;
            }
            // Check if op_b is used but undefined
            if ((instr.op == Opcode::add || instr.op == Opcode::sub || 
                instr.op == Opcode::mulu) && op_b == UINT32_MAX) {
                op_b = next_reg++;
            }
            
            new_use[id] = {op_a, op_b};
        }
    }
    // Extract the sizes before adding new instructions
    m_orig_program_size = m_program.size();

    // Original new_dest and new_use sizes
    std::vector<uint32_t> orig_new_dest = new_dest;
    std::vector<std::pair<uint32_t, uint32_t>> orig_new_use = new_use;

    // Check for self-dependencies within the loop
    if (basic_blocks.size() > 1) {
        const auto& bb1 = basic_blocks[1];

        // Step 1: Build a map of register initializations in the loop
        std::unordered_map<uint32_t, bool> reg_initialized_in_loop;
        
        // First scan to find which registers are loaded/initialized at the start of the loop
        for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
            const auto& instr = m_program[i];
            
            // If this is a load or mov instruction, mark the destination register as initialized
            if (instr.op == Opcode::ld || instr.op == Opcode::movi) {
                reg_initialized_in_loop[instr.dest] = true;
            }
        }
        
        // Step 2: Now scan for self-dependencies (registers that are read and written)
        for (uint64_t i = bb1.first; i < bb1.second - 1; ++i) {
            const auto& instr = m_program[i];
            
            // Skip instructions that don't modify registers
            if (instr.op == Opcode::st || instr.op == Opcode::loop || instr.op == Opcode::nop) {
                continue;
            }
            
            // Only consider registers as having a self-dependency if they're not initialized in the loop
            bool has_self_dependency = false;
            uint32_t reg_to_check = UINT32_MAX;
            
            // Check for self-dependencies
            if (instr.op == Opcode::addi && instr.dest == instr.op_a) {
                has_self_dependency = true;
                reg_to_check = instr.dest;
            } else if ((instr.op == Opcode::add || instr.op == Opcode::sub || instr.op == Opcode::mulu) &&
                      (instr.dest == instr.op_a || instr.dest == instr.op_b)) {
                has_self_dependency = true;
                reg_to_check = instr.dest;
            }
            
            // If we found a self-dependency and the register isn't initialized in the loop,
            // we need to add a mov instruction
            if (has_self_dependency && reg_to_check != UINT32_MAX && 
                !reg_initialized_in_loop[reg_to_check]) {
                need_mov_phase3.push_back({i, i});
                std::cout << "Added self-dependency mov for: " << i << " dest=" << instr.dest 
                         << " (not initialized in loop)" << std::endl;
            }
        }
    }

    // Calculate max latest time for all mov instructions for the same bundle
    uint64_t global_lowest_time = m_time_end_of_loop;
    
    // First calculate the latest start time among all mov instructions
    for (const auto& [bb0_id, bb1_id] : need_mov_phase3) {
        uint64_t ins_latency = (m_program[bb1_id].op == Opcode::mulu) ? 3 : 1;
        uint64_t lowest_time = std::max(m_time_end_of_loop, time_table[bb1_id] + ins_latency);
        global_lowest_time = std::max(global_lowest_time, lowest_time);
    }

    // Create vectors to collect all mov instructions and their register allocations
    std::vector<uint64_t> mov_instruction_ids;
    std::vector<uint32_t> mov_dest_regs;
    std::vector<std::pair<uint32_t, uint32_t>> mov_use_regs;
    
    // Create a mutable copy of time_table for passing to insert_mov_end_of_loop
    std::vector<uint64_t> mutable_time_table = time_table;
    
    // First create all mov instructions
    for (const auto& [bb0_id, bb1_id] : need_mov_phase3) {
        // Create a new mov instruction
        uint64_t movr_id = m_program.size();
        mov_instruction_ids.push_back(movr_id);

        // Create and add the mov instruction
        // For self-dependencies, we need special handling
        if (bb0_id == bb1_id) {
            // We're looking for something like this:
            // mov x1, x2  (x1 is the source from BB0, x2 is the modified register in the loop)
            
            // Debug print to understand the registers
            std::cout << "Self-dependency mov registers: " << std::endl;
            std::cout << "- Original instruction: " << m_program[bb0_id].to_string() << std::endl;
            std::cout << "- Original dest: " << m_program[bb0_id].dest << std::endl;
            std::cout << "- Original op_a: " << m_program[bb0_id].op_a << std::endl;
            std::cout << "- Renamed dest: " << orig_new_dest[bb1_id] << std::endl;
            
            // For addi x2, x2, 1, we need mov x1, x2 where:
            // - x1 is the register holding the input value (could be original x2 or a new register)
            // - x2 is the loop-allocated destination register
            uint32_t src_reg = orig_new_use[bb0_id].first; // Source register in the loop
            uint32_t dst_reg = orig_new_dest[bb0_id];      // Destination register in the loop
            
            m_program.push_back(Instruction{
                .op = Opcode::movr,
                .dest = src_reg,   // Source register becomes destination of mov
                .op_a = dst_reg    // Destination register becomes source of mov
            });
            
            std::cout << "- Created mov with dest=" << src_reg << ", op_a=" << dst_reg << std::endl;
            
            // Update register allocation info for this mov instruction
            mov_dest_regs.push_back(src_reg);
            mov_use_regs.push_back({dst_reg, UINT32_MAX});
        } else {
            // For regular interloop dependencies
            m_program.push_back(Instruction{
                .op = Opcode::movr,
                .dest = orig_new_dest[bb0_id],  // BB0 destination register
                .op_a = orig_new_dest[bb1_id]   // BB1 source register
            });
            
            // Update register allocation info for this mov instruction
            mov_dest_regs.push_back(orig_new_dest[bb0_id]);
            mov_use_regs.push_back({orig_new_dest[bb1_id], UINT32_MAX});
        }
    }
    
    // Now adjust the loop bundle to have both mov instructions if needed
    if (!mov_instruction_ids.empty()) {
        // Get the loop instruction and its position
        const auto loop_instr_id = m_bundles.at(m_time_end_of_loop - 1).at(4);
        auto cur_time = m_time_end_of_loop - 1;
        
        // Shift the loop instruction if needed to accommodate our global_lowest_time
        while (cur_time < global_lowest_time) {
            m_bundles.at(cur_time).at(4) = -1;
            m_bundles.insert(m_bundles.begin() + cur_time + 1, Bundle{-1, -1, -1, -1, -1});
            m_bundles.at(cur_time + 1).at(4) = loop_instr_id;
            ++cur_time;
        }
        
        // Insert the mov instructions into the loop bundle
        // Try to put both mov instructions in the same bundle
        uint64_t slot_idx = 0;
        for (size_t i = 0; i < mov_instruction_ids.size(); ++i) {
            // Find an empty ALU slot (ALU0 or ALU1)
            while (slot_idx < 2 && m_bundles.at(cur_time).at(slot_idx) != -1) {
                ++slot_idx;
            }
            
            // If no slots available in this bundle, create a new bundle
            if (slot_idx >= 2) {
                m_bundles.at(cur_time).at(4) = -1;
                m_bundles.insert(m_bundles.begin() + cur_time + 1, Bundle{-1, -1, -1, -1, -1});
                m_bundles.at(cur_time + 1).at(4) = loop_instr_id;
                ++cur_time;
                slot_idx = 0;
            }
            
            // Insert the mov instruction and update time_table
            m_bundles.at(cur_time).at(slot_idx) = mov_instruction_ids[i];
            if (mov_instruction_ids[i] >= mutable_time_table.size()) {
                mutable_time_table.resize(m_program.size(), UINT64_MAX);
            }
            mutable_time_table[mov_instruction_ids[i]] = cur_time;
            
            // Update register allocation
            new_dest.push_back(mov_dest_regs[i]);
            new_use.push_back(mov_use_regs[i]);
            
            // Move to next slot for the next instruction
            ++slot_idx;
        }
    }

    return {new_dest, new_use};
}

