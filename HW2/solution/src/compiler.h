#ifndef COMPILER_H
#define COMPILER_H



#include "common.h"

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
    std::vector<std::pair<uint64_t, uint64_t>> find_basic_blocks() const;
    Program m_program;
};



#endif //COMPILER_H
