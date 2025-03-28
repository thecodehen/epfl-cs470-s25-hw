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

struct active_list_entry_t {
  bool done;
  bool exception;
  reg_t logical_destination;
  reg_t old_destination;
  pc_t pc;
};

struct integer_queue_entry_t {
  reg_t dest_register;
  bool op_a_is_ready;
  reg_t op_a_reg_tag;
  operand_t op_a_value;
  bool op_b_is_ready;
  reg_t op_b_reg_tag;
  operand_t op_b_value;
  opcode op;
  pc_t pc;
};

struct alu_queue_entry_t {
  reg_t dest_register;
  operand_t op_a_value;
  operand_t op_b_value;
  opcode op;
  pc_t pc;
};

struct alu_result_t {
  reg_t dest_register;
  operand_t result;
  bool exception;
  pc_t pc;
};

constexpr uint32_t logical_register_file_size = 32;
constexpr uint32_t physical_register_file_size = 64;
constexpr uint32_t active_list_size = 32;
constexpr uint32_t integer_queue_size = 32;
constexpr uint32_t num_alus {4};
constexpr uint32_t max_commit_instructions {4};

#endif //COMMON_H
