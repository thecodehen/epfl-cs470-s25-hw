#ifndef COMPILER_H
#define COMPILER_H



#include "common.h"

/*
 * Dependency stores the dependencies for a given instruction. The dependencies
 * are based on the instruction's position in the program.
 */
struct Dependency {
    std::vector<uint64_t> local;
    std::vector<uint64_t> interloop;
    std::vector<uint64_t> loop_invariant;
    std::vector<uint64_t> post_loop;
};

typedef std::pair<uint64_t, uint64_t> Block;

class Compiler {
public:
    Compiler(const Program& program)
        : m_program{program} {}
    virtual VLIWProgram compile() = 0;
protected:
    uint32_t compute_min_initiation_interval() const;

    /**
     * Find the basic blocks in the program.
     * Returns a vector of pairs, where each pair represents a basic block.
     * The second element of each pair is not included in the basic block, i.e.,
     * [start, end).
     */
    std::vector<Block> find_basic_blocks() const;

    /**
     * Find the dependencies for a given Program.
     */
    std::vector<Dependency> find_dependencies(std::vector<Block> blocks) const;

    Program m_program;
private:
    /**
      * Check if the instruction is a consumer of any producers. Returns the
      * instruction addresses and the register id of the producers.
      * @return {instruction id, register id}
      */
    std::vector<std::pair<uint32_t, uint32_t>> find_instr_dependency(
        const std::array<int32_t, num_registers_with_special>& producers,
        const Instruction& instr
    ) const;

    void update_producers(
        std::array<int32_t, num_registers_with_special>& producers,
        const int32_t instr_idx
    ) const;
};



#endif //COMPILER_H
