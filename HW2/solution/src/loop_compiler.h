#ifndef LOOP_COMPILER_H
#define LOOP_COMPILER_H



#include "common.h"
#include "compiler.h"

class LoopCompiler : public Compiler {
public:
    LoopCompiler(const Program& program)
        : Compiler{program} {}
    VLIWProgram compile() override;
};



#endif //LOOP_COMPILER_H
