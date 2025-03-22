#ifndef DECODE_UNIT_H
#define DECODE_UNIT_H



#include "common.h"
#include "processor_state.h"

const uint32_t max_decode_instructions {4};

class decode_unit {
public:
  void step(processor_state& state, const program_t& program);
private:
  instruction_t decode(const std::string& instruction);
};



#endif //DECODE_UNIT_H
