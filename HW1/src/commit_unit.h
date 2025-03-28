#ifndef COMMIT_UNIT_H
#define COMMIT_UNIT_H



#include "common.h"
#include "processor_state.h"

class commit_unit {
public:
  void step(processor_state& state);
};



#endif //COMMIT_UNIT_H
