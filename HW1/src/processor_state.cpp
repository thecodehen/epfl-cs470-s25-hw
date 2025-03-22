#include "processor_state.h"

processor_state::processor_state()
  : active_list(active_list_size),
    integer_queue(integer_queue_size) {
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
}

json processor_state::to_json() const {
  json j;
  j["PC"] = pc;
  j["PhysicalRegisterFile"] = physical_register_file;
  j["DecodedPCs"] = decoded_pcs;
  j["ExceptionPC"] = exception_pc;
  j["Exception"] = exception;
  j["RegisterMapTable"] = register_map_table;
  j["FreeList"] = free_list;
  j["BusyBitTable"] = busy_bit_table;
  json::array_t active_list_json;
  for (auto &entry : active_list.to_vector()) {
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
  for (auto &entry : integer_queue.to_vector()) {
    json::object_t object;
    object["DestRegister"] = entry.dest_register;
    object["OpAIsReady"] = entry.op_a_is_ready;
    object["OpARegTag"] = entry.op_a_reg_tag;
    object["OpAValue"] = entry.op_a_value;
    object["OpBIsReady"] = entry.op_b_is_ready;
    object["OpBRegTag"] = entry.op_b_reg_tag;
    object["OpBValue"] = entry.op_b_value;
    object["Op"] = entry.op;
    object["PC"] = entry.pc;
    integer_queue_json.push_back(object);
  }
  j["IntegerQueue"] = integer_queue_json;
  return j;
}