#include "alu_unit.h"

#include <iostream>

void alu_unit::step(processor_state &state) {
  // check if we have backpressure
  if (!state.alu_results.at(m_alu_id).empty()) {
    return;
  }

  // check if there are instructions to execute
  if (state.alu_queues.at(m_alu_id).empty()) {
    return;
  }
  std::cout << "alu " << m_alu_id << " executing " << state.alu_queues.at(m_alu_id).front().pc << '\n';

  // get the instruction
  auto queue_entry = state.alu_queues.at(m_alu_id).front();
  state.alu_queues.at(m_alu_id).pop();

  // compute the result
  alu_result_t result {};
  result.dest_register = queue_entry.dest_register;
  result.exception = false;
  result.pc = queue_entry.pc;

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

  // push the result to the result queue
  state.alu_results.at(m_alu_id).push(result);
}
