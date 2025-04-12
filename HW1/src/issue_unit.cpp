#include "issue_unit.h"

#include <iostream>

void issue_unit::step(processor_state& state) {
  // check if there are instructions to issue
  if (state.integer_queue.empty()) {
    return;
  }

  // check if we have an exception
  if (state.exception) {
    return;
  }

  // forward results from ALU
  forward_from_alu_results(state);

  // loop through all instructions in the integer queue and check if any is ready
  auto it = state.integer_queue.begin();
  while (it != state.integer_queue.end()) {
    auto& entry = *it;
    bool should_remove {false};
    if (entry.op_a_is_ready && entry.op_b_is_ready) {
      // check if there is an available ALU
      for (auto& alu_queue : state.alu_queues) {
        if (alu_queue.empty()) {
          alu_queue.push({
            .dest_register = entry.dest_register,
            .op_a_value = entry.op_a_value,
            .op_b_value = entry.op_b_value,
            .op = entry.op,
            .pc = entry.pc,
          });
          should_remove = true;
          break;
        }
      }

      // remove the instruction from the integer queue if issued
      if (should_remove) {
        std::cout << "issuing instruction at pc: " << entry.pc << '\n';
        it = state.integer_queue.erase(it);
      }
    }

    if (!should_remove) {
      it = std::next(it);
    }
  }
}

void issue_unit::forward_from_alu_results(processor_state& state) const {
  // loop through all instructions in the active list and check if any is done
  for (auto& entry : state.integer_queue) {
    std::optional<operand_t> result_a = state.lookup_from_alu_forward_results(entry.op_a_reg_tag);
    if (result_a.has_value()) {
      entry.op_a_is_ready = true;
      entry.op_a_reg_tag = 0;
      entry.op_a_value = result_a.value();
    }

    std::optional<operand_t> result_b = state.lookup_from_alu_forward_results(entry.op_b_reg_tag);
    if (result_b.has_value()) {
      entry.op_b_is_ready = true;
      entry.op_b_reg_tag = 0;
      entry.op_b_value = result_b.value();
    }
  }
}