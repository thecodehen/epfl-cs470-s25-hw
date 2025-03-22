#ifndef RENAME_UNIT_H
#define RENAME_UNIT_H



#include "common.h"
#include "processor_state.h"

class rename_unit {
public:
  void step(processor_state& state);
};



#endif //RENAME_UNIT_H
