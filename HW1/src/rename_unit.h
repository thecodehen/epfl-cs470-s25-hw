#ifndef RENAME_UNIT_H
#define RENAME_UNIT_H



#include "processor_state.h"

class rename_unit {
public:
  void step(processor_state& state);
private:
  void clear(processor_state& state);
};



#endif //RENAME_UNIT_H
