#ifndef SIMULATOR_H
#define SIMULATOR_H



#include <vector>
#include "common.h"
#include "decode_unit.h"
#include "processor_state.h"
#include "rename_unit.h"


class simulator {
public:
  explicit simulator(const program_t &program)
      : m_program(program) {};
  void step();
  json get_json_state();
private:
  program_t m_program;
  processor_state m_processor_state;
  decode_unit m_decode_unit;
  rename_unit m_rename_unit;
};



#endif //SIMULATOR_H
