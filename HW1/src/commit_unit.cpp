#include "commit_unit.h"

#include <iostream>

void commit_unit::step(processor_state& state) {
  // check if there are instructions to commit
  int num_committed_instructions {0};

  for (int i = 0; i < num_alus; ++i) {
    auto& alu_result = state.alu_results.at(i);
    std::cout << "alu " << i << ": ";
    if (!alu_result.empty()) {
      std::cout << alu_result.front().pc;
    }
    std::cout << "\n";
  }
  std::cout << "\n";

  for (auto& active_list_entry : state.active_list) {
    // check if we have committed the maximum number of instructions
    if (num_committed_instructions >= max_commit_instructions) {
      break;
    }

    // check if we have the result in one of the ALUs
    for (auto& alu_result : state.alu_results) {
      if (!alu_result.empty()) {
        auto& result = alu_result.front();
        // TODO: handle exceptions
        if (result.pc == active_list_entry.pc) {
          // commit the instruction
          active_list_entry.done = true;
          active_list_entry.exception = result.exception;
          state.physical_register_file.at(result.dest_register) = result.result;
          state.free_list.push_back(active_list_entry.old_destination);
          state.busy_bit_table.at(active_list_entry.old_destination) = false;
          num_committed_instructions++;
          alu_result.pop();
          break;
        }
      }
    }
  }
}