#include "alu_unit.h"

#include <iostream>

void alu_unit::step(processor_state &state) {
  // check if we have a result in the second cycle and propagate it to the processor state
  if (has_result) {
    state.alu_results.at(m_alu_id).push(m_alu_result);
    has_result = false;
  }

  // check if there are queue_entryuctions to execute in the first stage
  if (state.alu_queues.at(m_alu_id).empty()) {
    return;
  }

  // get the queue_entryuction
  auto queue_entry = state.alu_queues.at(m_alu_id).front();
  state.alu_queues.at(m_alu_id).pop();

  // compute the result
  alu_result_t result {};
  result.dest_register = queue_entry.dest_register;
  result.exception = false;

  switch (queue_entry.op) {
    case opcode::add:
    case opcode::addi:
      result.result = queue_entry.op_a_value + queue_entry.op_b_value;
      break;
    case opcode::sub:
      result.result = queue_entry.op_a_value - queue_entry.op_b_value;
      break;
    case opcode::mulu:
      result.result = queue_entry.op_a_value * queue_entry.op_b_value;
      break;
    case opcode::divu:
      // raise exception if the divisor is zero
      if (queue_entry.op_b_value == 0) {
        result.exception = true;
        break;
      }
      result.result = queue_entry.op_a_value / queue_entry.op_b_value;
      break;
    case opcode::remu:
      // raise exception if the divisor is zero
      if (queue_entry.op_b_value == 0) {
        result.exception = true;
        break;
      }
      result.result = queue_entry.op_a_value % queue_entry.op_b_value;
      break;
    default:
      std::cerr << "Unknown opcode: " << static_cast<uint32_t>(queue_entry.op) << '\n';
      break;
  }

  has_result = true;
  m_alu_result = result;
}
