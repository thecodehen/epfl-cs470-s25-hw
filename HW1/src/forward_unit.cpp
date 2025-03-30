#include "forward_unit.h"

void forward_unit::step(processor_state &state) {
  state.alu_forward_results.clear();
  for (uint32_t alu_id {0}; alu_id < num_alus; ++alu_id) {
    // check if there are results to forward
    if (!state.alu_results.at(alu_id).empty()) {
      // copy the result to the forward results
      state.alu_forward_results.push_back(state.alu_results.at(alu_id).front());
    }
  }
}
