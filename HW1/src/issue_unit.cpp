#include "issue_unit.h"

#include <iostream>

void issue_unit::step(processor_state& state) {
  // check if there are instructions to issue
  if (state.integer_queue.empty()) {
    return;
  }

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
          });
          should_remove = true;
          break;
        }
      }

      // remove the instruction from the integer queue if issued
      if (should_remove) {
        it = state.integer_queue.erase(it);
      }
    }

    if (!should_remove) {
      it = std::next(it);
    }
  }
}