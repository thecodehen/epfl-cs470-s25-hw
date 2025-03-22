#ifndef ISSUE_UNIT_H
#define ISSUE_UNIT_H



#include "processor_state.h"

class issue_unit {
public:
  void step(processor_state& state);
};



#endif //ISSUE_UNIT_H
