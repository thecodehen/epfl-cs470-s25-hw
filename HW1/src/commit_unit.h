#ifndef COMMIT_UNIT_H
#define COMMIT_UNIT_H



#include "processor_state.h"

class commit_unit {
public:
  void step(processor_state& state);
  void exception_step(processor_state& state);
private:
  void propagate_alu_forwarding_results(processor_state& state);
};



#endif //COMMIT_UNIT_H
