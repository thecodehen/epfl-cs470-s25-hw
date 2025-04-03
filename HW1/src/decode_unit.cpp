#include <string>
#include "decode_unit.h"

void decode_unit::step(processor_state& state, const program_t& program) {
  // check if there is an instruction to decode
  if (state.pc >= program.size()) {
    return;
  }

  if (state.exception) {
    state.decoded_pcs.clear();
    return;
  }

  // check if the next stage (rename and dispatch stage) is applying backpressure
  if (!state.decoded_pcs.empty()) {
    return;
  }

  // fetch the next instructions to decode
  for (uint32_t i = 0; i < max_decode_instructions; ++i) {
    state.decoded_pcs.push_back({
      state.pc,
      decode(program[state.pc]),
    });
    state.pc++;
  }
}

instruction_t decode_unit::decode(const std::string& instruction) {
  instruction_t instr {};
  std::stringstream ss(instruction);

  // decode the opcode
  std::string op;
  ss >> op;
  if (op == "add") {
    instr.op = opcode::add;
  } else if (op == "addi") {
    instr.op = opcode::addi;
  } else if (op == "sub") {
    instr.op = opcode::sub;
  } else if (op == "mulu") {
    instr.op = opcode::mulu;
  } else if (op == "divu") {
    instr.op = opcode::divu;
  } else if (op == "remu") {
    instr.op = opcode::remu;
  }

  // decode the destination, i.e., dest = "x10,"
  std::string dest;
  ss >> dest;
  instr.dest = std::stoi(dest.substr(1));

  // decode the first operand, i.e., op_a = "x1,"
  std::string op_a;
  ss >> op_a;
  instr.op_a = std::stoi(op_a.substr(1));

  // decode the second operand, i.e., op_b = "x2," or an immediate value
  if (instr.op == opcode::addi) {
    std::string imm;
    ss >> imm;
    instr.imm = std::stoll(imm);
  } else {
    std::string op_b;
    ss >> op_b;
    instr.op_b = std::stoi(op_b.substr(1));
  }

  return instr;
}