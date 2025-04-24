#ifndef COMPILER_H
#define COMPILER_H



#include "common.h"

class Compiler {
public:
    virtual VLIWProgram compile(const Program& program) = 0;
};



#endif //COMPILER_H
