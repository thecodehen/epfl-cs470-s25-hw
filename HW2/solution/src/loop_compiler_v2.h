#ifndef LOOP_COMPILER_H
#define LOOP_COMPILER_H

#include "common.h"
#include "compiler.h"
#include <array>

class LoopCompiler : public Compiler {
public:
    LoopCompiler(const Program& program)
        : Compiler{program} {}
    
    /**
     * Main compilation method that orchestrates the loop scheduling process
     * and returns the final VLIW program
     */
    VLIWProgram compile() override;
    
private:
    /** 
     * Bundle representation: [ALU0, ALU1, MUL, MEM, BRANCH]
     * Stores instruction indices: -1 for empty, 0..N-1 for instruction index
     * Matches the structure used in the VLIWProgram output
     */
    using Bundle = std::array<int32_t, 5>;
    
    /** 
     * Collection of bundles that form the schedule
     * Each bundle represents one cycle of execution
     */
    std::vector<Bundle> m_bundles;
    
    /**
     * Time markers for loop execution (used in non-pipelined version too)
     * m_time_start_of_loop: First bundle of the loop body
     * m_time_end_of_loop: Last bundle of the loop body
     */
    uint64_t m_time_start_of_loop = 0;
    uint64_t m_time_end_of_loop = 0;
    
    /**
     * Track the original program size before adding mov instructions
     */
    uint64_t m_orig_program_size = 0;

    /**
     * Main scheduling function - orchestrates the scheduling of all basic blocks
     * 
     * @param dependencies Dependencies between instructions
     * @param basic_blocks Basic blocks of the program
     * @return Vector mapping instruction IDs to their scheduled bundle IDs
     */
    std::vector<uint64_t> schedule(
        std::vector<Dependency>& dependencies,
        const std::vector<Block>& basic_blocks);
    
    /**
     * Schedule basic block 0 (pre-loop instructions) with ASAP algorithm
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @param basic_blocks Basic blocks of the program
     * @param dependencies Dependencies between instructions
     */
    void schedule_bb0(
        std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies);
    
    /**
     * Schedule basic block 1 (loop body instructions) with ASAP algorithm
     * respecting loop dependencies
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @param basic_blocks Basic blocks of the program
     * @param dependencies Dependencies between instructions
     */
    void schedule_bb1(
        std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies);
    
    /**
     * Schedule basic block 2 (post-loop instructions) with ASAP algorithm
     * respecting dependencies from previous blocks
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @param basic_blocks Basic blocks of the program
     * @param dependencies Dependencies between instructions
     */
    void schedule_bb2(
        std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies);

    /**
     * Attempt to schedule an instruction ASAP (As Soon As Possible)
     * in a bundle that has the required functional unit available
     * 
     * @param instr_id ID of the instruction to schedule
     * @param lowest_time Earliest possible time based on dependencies
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return true if successfully scheduled, false if no slot available
     */
    bool schedule_instruction_asap(
        uint64_t instr_id,
        uint64_t lowest_time,
        std::vector<uint64_t>& time_table);
    
    /**
     * Create a new bundle for an instruction when ASAP scheduling fails
     * 
     * @param instr_id ID of the instruction to schedule
     * @param lowest_time Earliest possible time based on dependencies
     * @param time_table Mapping of instruction IDs to bundle IDs
     */
    void append_instruction(
        uint64_t instr_id,
        uint64_t lowest_time,
        std::vector<uint64_t>& time_table);
    
    void insert_mov_end_of_loop(const uint32_t instr_id, const uint64_t lowest_time, std::vector<uint64_t>& time_table);
    /**
     * Perform register allocation using a simplified algorithm:
     * 1. Allocate destination registers
     * 2. Link operands to their producer registers
     * 3. Fix interloop dependencies
     * 4. Assign registers to undefined operands
     * 
     * @param dependencies Dependencies between instructions
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @param basic_blocks Basic blocks of the program
     * @return std::pair containing: 
     *         1. Vector of newly allocated destination registers for each instruction
     *         2. Vector of pairs for operand registers (op_a, op_b) for each instruction
     */
    std::pair<std::vector<uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> 
    allocate_registers(
        const std::vector<Dependency>& dependencies, 
        const std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks);
};

#endif //LOOP_COMPILER_H