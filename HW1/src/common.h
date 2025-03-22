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

// program counter data type
typedef uint32_t pc_t;

// register name type
typedef uint32_t reg_t;

// operand data type
typedef uint64_t operand_t;

// instruction data type
struct instruction_t {
  opcode op;
  uint32_t dest;
  uint32_t op_a;
  uint32_t op_b;
  operand_t imm;
};

constexpr uint32_t logical_register_file_size = 32;
constexpr uint32_t physical_register_file_size = 64;
constexpr uint32_t active_list_size = 32;
constexpr uint32_t integer_queue_size = 32;

#endif //COMMON_H
