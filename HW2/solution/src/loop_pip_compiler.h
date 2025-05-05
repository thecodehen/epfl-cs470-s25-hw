#ifndef LOOP_PIP_COMPILER_H
#define LOOP_PIP_COMPILER_H

#include "common.h"
#include "compiler.h"
#include <array>
#include <vector>

using BundlePtr = std::array<Instruction*, 5>;
using Bundle = std::array<Instruction, 5>;

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
    

    /**
     * Collection of bundles that form the schedule
     * Each bundle represents one cycle of execution
     */
    std::vector<BundlePtr> m_bundles;
    
    /**
     * Stores which slots are reserved in each bundle due to modulo scheduling
     * Parallel array to m_bundles where RESERVED means the slot is reserved
     */
    std::vector<std::array<SlotStatus, 5>> m_slot_status;
    
    /**
     * Maps each bundle to its pipeline stage
     * Used to track which bundles belong to which stages
     */
    std::vector<std::vector<uint64_t>> m_pipeline_stages;

    /**
     * Maps each instruction to its corresponding pipeline stage
     */
    std::unordered_map<uint64_t, uint64_t> m_instruction_to_stage_map;
    
    /**
     * Tracks which instructions need predication and with which predicate register
     * Maps instruction IDs to predicate register IDs
     */
    std::vector<uint32_t> m_predicate_map;
    
    /**
     * Time markers for loop execution
     * m_loop_start_time: First bundle of the loop body
     * m_loop_end_time: Last bundle of the loop body
     */
    uint64_t m_loop_start_time = 0;
    uint64_t m_loop_end_time = 0;
    
    /**
     * Current Initiation Interval
     * Will be adjusted if scheduling fails with current II
     */
    uint64_t m_initiation_interval {};

    /**
     * Main scheduling function for pipelined execution
     * Implements modulo scheduling with resource reservation
     *
     * @param basic_blocks Basic blocks of the program
     * @param dependencies Dependency information for all instructions
     * @return Vector mapping instruction IDs to their bundle IDs
     */
    std::vector<uint64_t> schedule(
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies
        );
    
    /**
     * Schedules the first basic block (pre-loop code)
     * Identical to the non-pipelined version
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @param dependencies Dependency information for all instructions
     * @return
     */
    void schedule_preloop_block(
        std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies
        );
    
    /**
     * Schedules the loop body with pipeline support
     * Implements modulo scheduling with resource reservation
     * Will retry with increased II if scheduling fails
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return
     */
    void schedule_loop_body(
        std::vector<uint64_t>& time_table,
        const Block& bb1,
        const std::vector<Dependency>& dependencies
        );
    
    /**
     * Schedules the post-loop code
     * Similar to non-pipelined version but respects pipeline dependencies
     * 
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return
     */
    void schedule_postloop_block(std::vector<uint64_t>& time_table);
    
    /**
     * Helper function for pre-loop code (identical to LoopCompiler::insert_ASAP)
     * This is used for BB0 where we don't need modulo scheduling yet
     */
    void schedule_asap(
        std::vector<uint64_t>& time_table,
        uint64_t instr_id,
        uint64_t lowest_time
        );

    /**
     * try_schedule attempts to schedule the instruction at the specified time.
     * If unsuccessful, return false.
     */
    bool try_schedule(
        std::vector<uint64_t>& time_table,
        uint64_t instr_id,
        uint64_t time
        );

    /**
     * Checks if a slot is available for scheduling.
     *
     * @param instr_id
     * @return true if a slot is available, false otherwise
     */
    bool check_slot_available(uint64_t instr_id) const;

    bool try_schedule_modulo(
        std::vector<uint64_t>& time_table,
        uint64_t instr_id,
        uint64_t time
        );

    /**
     * Calculate the earliest possible start time for the loop based on dependencies
     */
    uint64_t calculate_loop_start_time(const std::vector<Dependency>& dependencies, 
                                     const Block& loop_block,
                                     const std::vector<uint64_t>& time_table);
    
    /**
     * Calculate the earliest possible time for an instruction based on all its dependencies
     */
    uint64_t calculate_instruction_earliest_time(uint64_t instr_id, 
                                              const std::vector<Dependency>& dependencies,
                                              const std::vector<uint64_t>& time_table,
                                              uint64_t loop_start_time);
    
    /**
     * Calculate additional time needed after the loop to handle interloop dependencies
     */
    uint64_t calculate_time_after_loop(const std::vector<Dependency>& dependencies,
                                     const std::vector<uint64_t>& loop_instructions,
                                     const std::vector<uint64_t>& time_table,
                                     uint64_t loop_start_time);
    
    /**
     * Attempts to insert an instruction with modulo scheduling
     * Reserves slots in other iterations according to the II
     * 
     * @param instr_id ID of the instruction to insert
     * @param earliest_time Earliest possible bundle based on dependencies
     * @param time_table Mapping of instruction IDs to bundle IDs
     * @return true if inserted successfully, false otherwise
     */
    bool schedule_asap_modulo(
        std::vector<uint64_t>& time_table,
        uint64_t instr_id,
        uint64_t earliest_time
        );
    
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
                                            std::vector<uint64_t>& time_table);
    
    /**
     * Propagates resource reservations when adding new bundles
     * Ensures that reserved slots maintain consistency across the schedule
     */
    void update_resource_reservations();
    
    /**
     * Checks if all interloop dependencies are satisfied with current II
     * Verifies the equation: S(P) + λ(P) ≤ S(C) + II
     * 
     * @param time_table Time table mapping instructions to bundles
     * @param loop_instructions Loop body instructions
     * @return true if all dependencies are satisfied, false otherwise
     */
    bool verify_pipeline_dependencies(const std::vector<uint64_t>& time_table, 
                                    const std::vector<uint64_t>& loop_instructions);
    
    /**
     * Organizes loop bundles into pipeline stages
     * Each stage has II bundles
     * Used for final code generation and predication
     */
    void organize_pipeline_stages();
    
    /**
     * Assigns predicate registers to instructions based on pipeline stage
     * First stage gets p32, second p33, etc.
     */
    void assign_predicate_registers(
        std::vector<Bundle>& bundles
    );
    
    /**
     * Creates initialization code for predicates and EC register
     * Adds mov instructions before the loop.pip instruction
     */
    void setup_pipeline_initialization(std::vector<Bundle>& bundles) const;

    void compress_pipeline(std::vector<Bundle>& bundles) const;

    std::vector<Bundle> rename(
        const std::vector<uint64_t>& time_table,
        const std::vector<Block>& basic_blocks,
        const std::vector<Dependency>& dependencies
    );

    /**
     * Allocate a new register for each instruction in the loop body that writes
     * a new value.
     */
    void rename_loop_body_dest();

    /**
     * Checks if instr consumes the old_dest register and renames it to
     * new_dest.
     *
     * @param old_dest the register to check if it is consumed in instr
     * @param new_dest the new register to rename the old_dest to
     * @param instr
     */
    static void rename_consumer_operands(
        uint32_t old_dest,
        uint32_t new_dest,
        Instruction& instr
    );

    /**
     * Renames registers in the program based on the loop invariant
     * dependencies. For some reason, we start allocating non-rotating registers
     * from x1 instead of x0. This code will directly modify the registers in
     * m_program.
     *
     * @param dependencies Dependency information for all instructions
     */
    void rename_loop_invariant(
        const std::vector<Dependency>& dependencies
    );

    /**
     * Renames the consumer instruction in the loop body.
     *
     * @param dependencies
     * @param bb1
     */
    void rename_loop_body_consumer(
        const std::vector<Dependency>& dependencies,
        const Block& bb1
    );

    /**
     * Renames the consumer instruction in the post-loop instructions.
     *
     * @param dependencies
     * @param bb2
     */
    void rename_post_loop_consumer(
        const std::vector<Dependency>& dependencies,
        const Block& bb2
    );
};

#endif //LOOP_PIP_COMPILER_H
