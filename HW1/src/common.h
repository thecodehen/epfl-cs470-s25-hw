#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <string>
#include <vector>

typedef std::vector<std::string> program_t;

enum class opcode {
  add,
  addi,
  sub,
  mulu,
  divu,
  remu,
};

struct instruction_t {
  opcode op;
  uint32_t dest;
  uint32_t op_a;
  uint32_t op_b;
  int64_t imm;
};

#endif //COMMON_H
