#ifndef LOOP_COMPILER_H
#define LOOP_COMPILER_H



#include "common.h"
#include "compiler.h"

class LoopCompiler : public Compiler {
public:
    VLIWProgram compile(const Program& program) override;
};



#endif //LOOP_COMPILER_H
