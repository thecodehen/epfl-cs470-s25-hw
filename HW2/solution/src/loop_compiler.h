#ifndef LOOP_COMPILER_H
#define LOOP_COMPILER_H



#include "common.h"
#include "compiler.h"
#include <array>

class LoopCompiler : public Compiler {
public:
    LoopCompiler(const Program& program)
        : Compiler{program} {}
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

    uint32_t m_next_non_rotating_reg {1};

    /** 
     * Main scheduling function - schedules all basic blocks
     * Returns a vector mapping instruction IDs to their scheduled bundle IDs
     */
    std::vector<uint64_t> schedule(std::vector<Dependency>& dependencies);
    
    /**
     * Schedule basic block 0 (pre-loop instructions)
     * Places instructions respecting local dependencies
     */
    std::vector<uint64_t> schedule_bb0(std::vector<uint64_t>& time_table);
    
    /**
     * Schedule basic block 1 (loop body instructions)
     * Handles loop-specific dependencies
     */
    std::vector<uint64_t> schedule_bb1(std::vector<uint64_t>& time_table);
    
    /**
     * Schedule basic block 2 (post-loop instructions)
     * Respects all dependencies including those from loop execution
     */
    std::vector<uint64_t> schedule_bb2(std::vector<uint64_t>& time_table);

    /**
     * Try to insert instruction ASAP (As Soon As Possible)
     * Finds the earliest bundle with an available functional unit of the right type
     * 
     * @param instr_id ID of the instruction to insert
     * @param lowest_time Earliest possible bundle (based on dependencies)
     * @param time_table Maps instruction IDs to bundle IDs
     * @return true if successfully inserted, false if no suitable slot found
     */
    bool insert_ASAP(uint64_t instr_id, uint64_t lowest_time, 
                     std::vector<uint64_t>& time_table);
    
    /**
     * Append a new bundle with the instruction when ASAP insertion fails
     * 
     * @param instr_id ID of the instruction to insert
     * @param lowest_time Earliest possible bundle (based on dependencies)
     * @param time_table Maps instruction IDs to bundle IDs
     */
    void append(uint64_t instr_id, uint64_t lowest_time,
               std::vector<uint64_t>& time_table);

    void rename_consumer_operands(
        const uint32_t old_dest,
        const uint32_t new_dest,
        Instruction& instr
    );

    /**
     * Inserts a mov instruction at the end of the loop. Maintains
     *  m_time_end_of_loop` to keep pointing to the end of the loop.
     * @param instr_id
     * @param lowest_time
     */
    void insert_mov_end_of_loop(
        const uint32_t instr_id,
        const uint64_t lowest_time);

    void rename(
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies,
        const std::vector<uint64_t>& time_table
    );

    /**
     * Inserts a mov instruction at the end of the loop
     * Used for handling interloop dependencies
     * 
     * @param dest_reg The destination register for the mov
     * @param src_reg The source register for the mov
     * @param time_table Mapping of instruction IDs to bundle IDs (will be updated)
     */
    void insert_mov_at_end_of_loop(uint32_t dest_reg, uint32_t src_reg,
                                 std::vector<uint64_t>& time_table);
                                 
    /**
     * Perform register allocation (allocb algorithm)
     * Implements the register allocation described in section 3.3.1
     * 1. Allocate unique registers to each instruction producing a value
     * 2. Link operands to the newly allocated registers
     * 3. Fix interloop dependencies with mov instructions
     * 4. Assign fresh registers to any undefined operands
     * 
     * @param dependencies Dependencies between instructions
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return std::pair containing: 
     *         1. Vector of newly allocated destination registers for each instruction
     *         2. Vector of pairs for operand registers (op_a, op_b) for each instruction
     */
    std::pair<std::vector<uint32_t>, std::vector<std::pair<uint32_t, uint32_t>>> 
    allocate_registers(const std::vector<Dependency>& dependencies, 
                      const std::vector<uint64_t>& time_table);
};



#endif //LOOP_COMPILER_H
