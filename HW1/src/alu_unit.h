#ifndef ALU_UNIT_H
#define ALU_UNIT_H



#include "processor_state.h"

class alu_unit {
public:
  explicit alu_unit(const uint32_t alu_id)
    : m_alu_id(alu_id) {}
  void step(processor_state& state);
private:
  uint32_t m_alu_id;
  void clear(processor_state& state);
};



#endif //ALU_UNIT_H
