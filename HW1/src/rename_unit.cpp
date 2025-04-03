#include "rename_unit.h"

#include <optional>

void rename_unit::step(processor_state& state) {
  // check if there is are instructions to rename
  if (state.decoded_pcs.empty()) {
    return;
  }

  if (state.exception) {
    state.integer_queue.clear();
  }

  // check if we have available space in the active list and integer queue
  unsigned long num_instructions_to_rename {state.decoded_pcs.size()};
  if (state.active_list.size() + num_instructions_to_rename > active_list_size) {
    return;
  }
  if (state.integer_queue.size() + num_instructions_to_rename > integer_queue_size) {
    return;
  }

  // rename the next instructions
  for (uint32_t i = 0; i < num_instructions_to_rename; ++i) {
    // get the next instruction to rename
    auto [pc, instr] = state.decoded_pcs.front();
    state.decoded_pcs.pop_front();

    // look up the value of the first operand
    bool op_a_is_ready {false};
    uint32_t op_a_reg_tag {state.register_map_table.at(instr.op_a)};
    operand_t op_a_value {0};
    std::optional<operand_t> result = state.lookup_from_alu_forward_results(op_a_reg_tag);
    if (!state.busy_bit_table.at(op_a_reg_tag)) {
      // we have the value so we don't need the tag anymore
      op_a_is_ready = true;
      op_a_value = state.physical_register_file.at(op_a_reg_tag);
    } else if (result.has_value()) {
      // we got the values from the forwarding path
      op_a_is_ready = true;
      op_a_value = result.value();
    }

    // look up the value of the second operand
    bool op_b_is_ready {false};
    uint32_t op_b_reg_tag {0};
    operand_t op_b_value {0};
    if (instr.op == opcode::addi) {
      // the second operand is an immediate value
      op_b_is_ready = true;
      op_b_value = instr.imm;
    } else {
      // look up the value of the second operand
      op_b_reg_tag = state.register_map_table.at(instr.op_b);
      std::optional<operand_t> result = state.lookup_from_alu_forward_results(op_b_reg_tag);
      if (!state.busy_bit_table.at(op_b_reg_tag)) {
        op_b_is_ready = true;
        op_b_value = state.physical_register_file.at(op_b_reg_tag);
      } else if (result.has_value()) {
        op_b_is_ready = true;
        op_b_value = result.value();
      }
    }

    // obtain a new physical register for the destination
    reg_t new_dest = state.free_list.front();
    state.free_list.pop_front();
    state.busy_bit_table[new_dest] = true;

    // rename the destination register
    reg_t old_dest = state.register_map_table[instr.dest];
    state.register_map_table[instr.dest] = new_dest;

    // add the instruction to the active list
    active_list_entry_t active_list_entry {
      .done = false,
      .exception = false,
      .logical_destination = instr.dest,
      .old_destination = old_dest,
      .pc = pc,
    };
    state.active_list.emplace_back(active_list_entry);

    // add the instruction to the integer queue
    integer_queue_entry_t integer_queue_entry {
      .dest_register = new_dest,
      .op_a_is_ready = op_a_is_ready,
      .op_a_reg_tag =  op_a_is_ready ? 0 : op_a_reg_tag,
      .op_a_value =    op_a_value,
      .op_b_is_ready = op_b_is_ready,
      .op_b_reg_tag =  op_b_is_ready ? 0 : op_b_reg_tag,
      .op_b_value =    op_b_value,
      .op =            instr.op,
      .pc =            pc,
    };
    state.integer_queue.emplace_back(integer_queue_entry);
  }
}