#include "simulator.h"
#include <iostream>

simulator::simulator(const program_t &program)
  : m_program(program) {
  for (int i = 0; i < num_alus; ++i) {
    m_alu_units.push_back(
      alu_unit(i)
    );
  }
}

bool simulator::can_step() const {
  // exception states
  if (m_processor_state.exception) {
    return true;
  }

  // not in exception state, but has exception before
  if (m_processor_state.has_exception) {
    return false;
  }

  return !m_processor_state.decoded_pcs.empty()
    || !m_processor_state.active_list.empty()
    || m_processor_state.pc < m_program.size();
}

void simulator::step() {
  // check if we can step
  if (!can_step()) {
    return;
  }

  // check if we have an exception
  if (m_processor_state.exception) {
    std::cout << "stepping exception...\n";
    exception_step();
  } else {
    std::cout << "stepping normal...\n";
    normal_step();
  }
}

void simulator::normal_step() {
  // process forwarding
  m_forward_unit.step(m_processor_state);
  m_commit_unit.step(m_processor_state);
  for (auto& alu_unit : m_alu_units) {
    alu_unit.step(m_processor_state);
  }
  m_issue_unit.step(m_processor_state);
  m_rename_unit.step(m_processor_state);
  m_decode_unit.step(m_processor_state, m_program);
}

void simulator::exception_step() {
  m_commit_unit.exception_step(m_processor_state);
}

json simulator::get_json_state() const {
  return m_processor_state.to_json();
}