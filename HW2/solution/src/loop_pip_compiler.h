#ifndef LOOP_PIP_COMPILER_H
#define LOOP_PIP_COMPILER_H



#include "common.h"
#include "compiler.h"

class LoopPipCompiler : public Compiler {
public:
    LoopPipCompiler(const Program& program)
        : Compiler{program} {}
    VLIWProgram compile() override;
};



#endif //LOOP_PIP_COMPILER_H
