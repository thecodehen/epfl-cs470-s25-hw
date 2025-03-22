#include "simulator.h"

void simulator::step() {
  // decode
  m_decode_unit.step(m_processor_state, m_program);
}

json simulator::get_json_state() {
  return m_processor_state.to_json();
}