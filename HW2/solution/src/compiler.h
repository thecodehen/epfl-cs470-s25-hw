#ifndef COMPILER_H
#define COMPILER_H



#include "common.h"

class Compiler {
public:
    Compiler(const Program& program)
        : m_program{program} {}
    virtual VLIWProgram compile() = 0;
private:
    uint32_t compute_min_initiation_interval();
    Program m_program;
};



#endif //COMPILER_H
