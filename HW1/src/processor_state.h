#ifndef PROCESSOR_STATE_H
#define PROCESSOR_STATE_H



#include <cstdint>
#include <vector>
#include <deque>
#include "common.h"
#include "json.hpp"
#include "max_size_queue.hpp"

using json = nlohmann::json;

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

class processor_state {
public:
  pc_t pc {};
  std::vector<uint64_t> physical_register_file;
  std::deque<std::pair<pc_t, instruction_t>> decoded_pcs;
  pc_t exception_pc {};
  bool exception {};
  std::vector<reg_t> register_map_table;
  std::deque<reg_t> free_list;
  std::vector<bool> busy_bit_table;
  std::deque<active_list_entry_t> active_list;
  std::deque<integer_queue_entry_t> integer_queue;
  processor_state();
  json to_json() const;
};



#endif //PROCESSOR_STATE_H
