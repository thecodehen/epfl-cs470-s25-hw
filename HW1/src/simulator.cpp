#include "simulator.h"

simulator::simulator(const program_t &program)
  : m_program(program) {
  for (int i = 0; i < num_alus; ++i) {
    m_alu_units.push_back(
      alu_unit(i)
    );
  }
}

void simulator::step() {
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

json simulator::get_json_state() {
  return m_processor_state.to_json();
}