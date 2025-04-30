#ifndef LOOP_COMPILER_H
#define LOOP_COMPILER_H



#include "common.h"
#include "compiler.h"

class LoopCompiler : public Compiler {
public:
    LoopCompiler(const Program& program)
        : Compiler{program} {}
    VLIWProgram compile() override;
private:
    /** Returns length of instructions and maps to the bundle ID
      */
    std::vector<uint64_t> schedule(std::vector<Dependency>& dependencies) const;
    std::vector<uint64_t> schedule_bb0(std::vector<uint64_t>& schedule);
    std::vector<uint64_t> schedule_bb1(std::vector<uint64_t>& schedule);
    std::vector<uint64_t> schedule_bb2(std::vector<uint64_t>& schedule);

    // rename() const;
};



#endif //LOOP_COMPILER_H
