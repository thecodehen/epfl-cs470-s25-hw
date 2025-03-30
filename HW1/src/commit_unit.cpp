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

  // check if we have results to commit
  for (auto it {state.active_list.begin()}; it != state.active_list.end();) {
    auto& active_list_entry = *it;

    // check if we have committed the maximum number of instructions
    if (num_committed_instructions >= max_commit_instructions) {
      break;
    }

    // check if the instruction is done
    if (!active_list_entry.done) {
      break;
    }

    // check if we have an exception
    // TODO: handle exceptions

    // commit the instruction
    state.free_list.push_back(active_list_entry.old_destination);
    state.busy_bit_table.at(active_list_entry.old_destination) = false;
    it = std::next(it);
    state.active_list.pop_front();
    num_committed_instructions++;
  }

  // change entires in the active list based on the forward results
  for (auto& alu_result : state.alu_results) {
    // TODO: we currently remove all alu results from the queue
    if (!alu_result.empty()) {
      alu_result.pop();
    }
  }
  for (auto& active_list_entry : state.active_list) {
    for (auto& alu_result : state.alu_forward_results) {
      if (alu_result.pc == active_list_entry.pc) {
        active_list_entry.done = true;
        active_list_entry.exception = alu_result.exception;
        state.physical_register_file.at(alu_result.dest_register) = alu_result.result;
        break;
      }
    }
  }
}