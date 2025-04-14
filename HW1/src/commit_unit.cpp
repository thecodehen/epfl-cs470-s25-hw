#include "commit_unit.h"

#include <iostream>

void commit_unit::step(processor_state& state) {
  // check if there are instructions to commit
  uint32_t num_committed_instructions {0};

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
    if (active_list_entry.exception) {
      std::cout << "exception! pc: " << active_list_entry.pc << "\n";
      state.has_exception = true;
      state.exception = true;
      state.exception_pc = active_list_entry.pc;
      state.pc = exception_pc_addr;
      break;
    }

    // commit the instruction
    state.free_list.push_back(active_list_entry.old_destination);
    it = std::next(it);
    state.active_list.pop_front();
    num_committed_instructions++;
  }
  propagate_alu_forwarding_results(state);
}

void commit_unit::exception_step(processor_state& state) {
  if (!state.exception) {
    std::cerr << "Error: exception_step called without an exception\n";
    return;
  }

  if (state.active_list.empty()) {
    // return back to normal state because the active list is empty
    state.exception = false;
  }

  for (uint32_t i {0}; !state.active_list.empty() && i < max_commit_instructions; ++i) {
    auto& active_list_entry {state.active_list.back()};

    reg_t cur_destination {state.register_map_table.at(active_list_entry.logical_destination)};
    reg_t old_destination {active_list_entry.old_destination};

    // put back the current destination to the free list
    state.free_list.push_back(cur_destination);

    // restore the old destination
    state.register_map_table.at(active_list_entry.logical_destination) = old_destination;

    // restore the busy bit
    state.busy_bit_table.at(cur_destination) = false;

    // remove the entry from the active list
    state.active_list.pop_back();
  }
}

void commit_unit::propagate_alu_forwarding_results(processor_state& state) {
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

        // TODO: Since these are not changing the active list, should we check this elsewhere?
        if (!alu_result.exception) {
          state.busy_bit_table.at(alu_result.dest_register) = false;
          state.physical_register_file.at(alu_result.dest_register) = alu_result.result;
        }
        break;
      }
    }
  }
}