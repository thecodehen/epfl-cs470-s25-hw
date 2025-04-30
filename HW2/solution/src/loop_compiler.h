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
     * Matches the structure used in the VLIWProgram output
     */
    using Bundle = std::array<const Instruction*, 5>;
    
    /** 
     * Collection of bundles that form the schedule
     * Each bundle represents one cycle of execution
     */
    mutable std::vector<Bundle> m_bundles;
    
    /**
     * Time markers for loop execution (used in non-pipelined version too)
     * m_time_start_of_loop: First bundle of the loop body
     * m_time_end_of_loop: Last bundle of the loop body
     */
    mutable uint64_t m_time_start_of_loop = 0;
    mutable uint64_t m_time_end_of_loop = 0;

    /** 
     * Main scheduling function - schedules all basic blocks
     * Returns a vector mapping instruction IDs to their scheduled bundle IDs
     */
    std::vector<uint64_t> schedule(std::vector<Dependency>& dependencies) const;
    
    /**
     * Schedule basic block 0 (pre-loop instructions)
     * Places instructions respecting local dependencies
     */
    std::vector<uint64_t> schedule_bb0(std::vector<uint64_t>& time_table) const;
    
    /**
     * Schedule basic block 1 (loop body instructions)
     * Handles loop-specific dependencies
     */
    std::vector<uint64_t> schedule_bb1(std::vector<uint64_t>& time_table) const;
    
    /**
     * Schedule basic block 2 (post-loop instructions)
     * Respects all dependencies including those from loop execution
     */
    std::vector<uint64_t> schedule_bb2(std::vector<uint64_t>& time_table) const;

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
                     std::vector<uint64_t>& time_table) const;
    
    /**
     * Append a new bundle with the instruction when ASAP insertion fails
     * 
     * @param instr_id ID of the instruction to insert
     * @param lowest_time Earliest possible bundle (based on dependencies)
     * @param time_table Maps instruction IDs to bundle IDs
     */
    void append(uint64_t instr_id, uint64_t lowest_time,
               std::vector<uint64_t>& time_table) const;
};



#endif //LOOP_COMPILER_H
