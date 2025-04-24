#ifndef LOOP_PIP_COMPILER_H
#define LOOP_PIP_COMPILER_H



#include "common.h"
#include "compiler.h"

class LoopPipCompiler : public Compiler {
public:
    VLIWProgram compile(const Program& program) override;
};



#endif //LOOP_PIP_COMPILER_H
