#ifndef LOOP_PIP_COMPILER_H
#define LOOP_PIP_COMPILER_H

#include "common.h"
#include "compiler.h"
#include <array>
#include <vector>

class LoopPipCompiler : public Compiler {
public:
    LoopPipCompiler(const Program& program)
        : Compiler{program} {}
    
    /**
     * Main compilation method that orchestrates the pipeline scheduling process
     * and returns the final VLIW program
     */
    VLIWProgram compile() override;

private:
    /**
     * Bundle representation: [ALU0, ALU1, MUL, MEM, BRANCH]
     * For pipeline scheduling, nullptr indicates an available slot,
     * while a reserved marker indicates a slot reserved due to pipeline stage overlap
     */
    enum SlotStatus {
        OPEN,      // Slot is available for instruction
        RESERVED   // Slot is reserved due to pipeline stage overlap
    };
    
    using Bundle = std::array<const Instruction*, 5>;
    
    /**
     * Collection of bundles that form the schedule
     * Each bundle represents one cycle of execution
     */
    mutable std::vector<Bundle> m_bundles;
    
    /**
     * Stores which slots are reserved in each bundle due to modulo scheduling
     * Parallel array to m_bundles where RESERVED means the slot is reserved
     */
    mutable std::vector<std::array<SlotStatus, 5>> m_slot_status;
    
    /**
     * Maps each bundle to its pipeline stage
     * Used to track which bundles belong to which stages
     */
    mutable std::vector<std::vector<uint64_t>> m_pipeline_stages;
    
    /**
     * Tracks which instructions need predication and with which predicate register
     * Maps instruction IDs to predicate register IDs
     */
    mutable std::vector<uint32_t> m_predicate_map;
    
    /**
     * Time markers for loop execution
     * m_loop_start_time: First bundle of the loop body
     * m_loop_end_time: Last bundle of the loop body
     */
    mutable uint64_t m_loop_start_time = 0;
    mutable uint64_t m_loop_end_time = 0;
    
    /**
     * Current Initiation Interval
     * Will be adjusted if scheduling fails with current II
     */
    mutable uint32_t m_initiation_interval = 0;

    /**
     * Main scheduling function for pipelined execution
     * Implements modulo scheduling with resource reservation
     * 
     * @param dependencies Dependency information for all instructions
     * @return Vector mapping instruction IDs to their bundle IDs
     */
    std::vector<uint64_t> schedule_with_pipelining(std::vector<Dependency>& dependencies) const;
    
    /**
     * Schedules the first basic block (pre-loop code)
     * Identical to the non-pipelined version
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return Updated time_table
     */
    std::vector<uint64_t> schedule_preloop_block(std::vector<uint64_t>& time_table) const;
    
    /**
     * Schedules the loop body with pipeline support
     * Implements modulo scheduling with resource reservation
     * Will retry with increased II if scheduling fails
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return Updated time_table
     */
    std::vector<uint64_t> schedule_loop_body_pipelined(std::vector<uint64_t>& time_table) const;
    
    /**
     * Schedules the post-loop code
     * Similar to non-pipelined version but respects pipeline dependencies
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return Updated time_table
     */
    std::vector<uint64_t> schedule_postloop_block(std::vector<uint64_t>& time_table) const;
    
    /**
     * Helper function for pre-loop code (identical to LoopCompiler::insert_ASAP)
     * This is used for BB0 where we don't need modulo scheduling yet
     */
    bool try_regular_insert(uint64_t instr_id, uint64_t lowest_time, 
                          std::vector<uint64_t>& time_table) const;
    
    /**
     * Helper function for pre-loop code (identical to LoopCompiler::append)
     */
    void append_regular_bundle(uint64_t instr_id, uint64_t lowest_time,
                             std::vector<uint64_t>& time_table) const;
    
    /**
     * Calculate the earliest possible start time for the loop based on dependencies
     */
    uint64_t calculate_loop_start_time(const std::vector<Dependency>& dependencies, 
                                     const Block& loop_block,
                                     const std::vector<uint64_t>& time_table) const;
    
    /**
     * Calculate the earliest possible time for an instruction based on all its dependencies
     */
    uint64_t calculate_instruction_earliest_time(uint64_t instr_id, 
                                              const std::vector<Dependency>& dependencies,
                                              const std::vector<uint64_t>& time_table,
                                              uint64_t loop_start_time) const;
    
    /**
     * Calculate additional time needed after the loop to handle interloop dependencies
     */
    uint64_t calculate_time_after_loop(const std::vector<Dependency>& dependencies,
                                     const std::vector<uint64_t>& loop_instructions,
                                     const std::vector<uint64_t>& time_table,
                                     uint64_t loop_start_time) const;
    
    /**
     * Attempts to insert an instruction with modulo scheduling
     * Reserves slots in other iterations according to the II
     * 
     * @param instr_id ID of the instruction to insert
     * @param earliest_time Earliest possible bundle based on dependencies
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return true if inserted successfully, false otherwise
     */
    bool try_modulo_insert(uint64_t instr_id, uint64_t earliest_time, 
                          std::vector<uint64_t>& time_table) const;
    
    /**
     * Appends a new bundle with modulo scheduling support
     * Used when insertion fails
     * Also handles resource reservation
     * 
     * @param instr_id ID of the instruction to insert
     * @param earliest_time Earliest possible bundle based on dependencies
     * @param time_table Mapping of instruction IDs to bundle IDs
     */
    void create_new_bundle_with_reservations(uint64_t instr_id, uint64_t earliest_time,
                                            std::vector<uint64_t>& time_table) const;
    
    /**
     * Propagates resource reservations when adding new bundles
     * Ensures that reserved slots maintain consistency across the schedule
     */
    void update_resource_reservations() const;
    
    /**
     * Checks if all interloop dependencies are satisfied with current II
     * Verifies the equation: S(P) + λ(P) ≤ S(C) + II
     * 
     * @param time_table Time table mapping instructions to bundles
     * @param loop_instructions Loop body instructions
     * @return true if all dependencies are satisfied, false otherwise
     */
    bool verify_pipeline_dependencies(const std::vector<uint64_t>& time_table, 
                                    const std::vector<uint64_t>& loop_instructions) const;
    
    /**
     * Organizes loop bundles into pipeline stages
     * Each stage has II bundles
     * Used for final code generation and predication
     */
    void organize_pipeline_stages() const;
    
    /**
     * Assigns predicate registers to instructions based on pipeline stage
     * First stage gets p32, second p33, etc.
     */
    void assign_predicate_registers() const;
    
    /**
     * Creates initialization code for predicates and EC register
     * Adds mov instructions before the loop.pip instruction
     */
    void setup_pipeline_initialization() const;
};

#endif //LOOP_PIP_COMPILER_H
