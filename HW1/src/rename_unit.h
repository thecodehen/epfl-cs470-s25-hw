#ifndef RENAME_UNIT_H
#define RENAME_UNIT_H



#include "common.h"
#include "processor_state.h"

class rename_unit {
public:
  void step(processor_state& state, const program_t& program);
};



#endif //RENAME_UNIT_H
