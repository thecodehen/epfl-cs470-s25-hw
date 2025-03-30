#ifndef SIMULATOR_H
#define SIMULATOR_H



#include <vector>
#include "alu_unit.h"
#include "commit_unit.h"
#include "common.h"
#include "decode_unit.h"
#include "forward_unit.h"
#include "issue_unit.h"
#include "processor_state.h"
#include "rename_unit.h"


class simulator {
public:
  explicit simulator(const program_t &program);
  void step();
  json get_json_state();
private:
  program_t m_program;
  processor_state m_processor_state;
  decode_unit m_decode_unit;
  rename_unit m_rename_unit;
  issue_unit m_issue_unit;
  std::vector<alu_unit> m_alu_units;
  forward_unit m_forward_unit;
  commit_unit m_commit_unit;
};



#endif //SIMULATOR_H
