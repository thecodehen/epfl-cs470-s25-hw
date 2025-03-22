#include "processor_state.h"

processor_state::processor_state() {
  // physical register file
  physical_register_file.resize(physical_register_file_size, 0);

  // register map table
  register_map_table.resize(logical_register_file_size, 0);
  for (reg_t i = 0; i < logical_register_file_size; ++i) {
    register_map_table[i] = i;
  }

  // free list
  for (reg_t i = 32; i < physical_register_file_size; ++i) {
    free_list.push_back(i);
  }

  // busy bit table
  busy_bit_table.resize(physical_register_file_size, false);

  // alu queues
  alu_queues.resize(num_alus);
  alu_results.resize(num_alus);
}

std::string opcode_to_string(const opcode op) {
  switch (op) {
    case opcode::add:
      return "add";
    case opcode::addi:
      return "add";
    case opcode::sub:
      return "sub";
    case opcode::mulu:
      return "mulu";
    case opcode::divu:
      return "divu";
    case opcode::remu:
      return "remu";
    default:
      return "unknown";
  }
}

json processor_state::to_json() const {
  json j;
  j["PC"] = pc;
  j["PhysicalRegisterFile"] = physical_register_file;
  json::array_t decoded_pcs_json;
  for (auto& entry : decoded_pcs) {
    decoded_pcs_json.push_back(entry.first);
  }
  j["DecodedPCs"] = decoded_pcs_json;
  j["ExceptionPC"] = exception_pc;
  j["Exception"] = exception;
  j["RegisterMapTable"] = register_map_table;
  j["FreeList"] = free_list;
  j["BusyBitTable"] = busy_bit_table;
  json::array_t active_list_json;
  for (auto& entry : active_list) {
    json::object_t object;
    object["Done"] = entry.done;
    object["Exception"] = entry.exception;
    object["LogicalDestination"] = entry.logical_destination;
    object["OldDestination"] = entry.old_destination;
    object["PC"] = entry.pc;
    active_list_json.push_back(object);
  }
  j["ActiveList"] = active_list_json;
  json::array_t integer_queue_json;
  for (auto& entry : integer_queue) {
    json::object_t object;
    object["DestRegister"] = entry.dest_register;
    object["OpAIsReady"] = entry.op_a_is_ready;
    object["OpARegTag"] = entry.op_a_reg_tag;
    object["OpAValue"] = entry.op_a_value;
    object["OpBIsReady"] = entry.op_b_is_ready;
    object["OpBRegTag"] = entry.op_b_reg_tag;
    object["OpBValue"] = entry.op_b_value;
    object["Op"] = opcode_to_string(entry.op);
    object["PC"] = entry.pc;
    integer_queue_json.push_back(object);
  }
  j["IntegerQueue"] = integer_queue_json;
  return j;
}