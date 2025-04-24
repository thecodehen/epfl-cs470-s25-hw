#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <vector>

enum class Opcode {
    add,
    addi,
    sub,
    mulu,
    ld,
    st,
    loop,
    loop_pip,
    nop,
    movr, // mov dest/LC/EC, src
    movi, // mov dest, imm
    movp, // mov pX, true/false
};

constexpr uint32_t num_registers = 96;
constexpr uint32_t num_predicates = 96;
constexpr uint32_t lc_id {num_registers};
constexpr uint32_t ec_id {num_registers + 1};

typedef uint64_t ProgramCounter;

class Instruction {
public:
    // add dest, op_a, op_b
    // addi dest, op_a, imm
    // sub dest, op_a, op_b
    // mulu dest, op_a, op_b
    // ld dest, imm(op_a)
    // st dest, imm(op_a)
    // loop imm
    // loop.pip imm
    // nop
    // mov pX, imm    -- movp
    // mov LC/EC, imm -- movi
    // mov dest, imm  -- movi
    // mov dest, op_a -- movr
    Opcode op;
    // dest also represents source for the st opcode
    uint32_t dest;
    // op_a stores source in mov
    uint32_t op_a;
    uint32_t op_b;
    // imm also stores loopStart, true=1/false=0
    int64_t imm;
    std::string to_string() const {
        std::string s;
        switch (op) {
        case Opcode::add:
            s += "add";
            break;
        case Opcode::addi:
            s += "addi";
            break;
        case Opcode::sub:
            s += "sub";
            break;
        case Opcode::mulu:
            s += "mulu";
            break;
        case Opcode::ld:
            s += "ld";
            break;
        case Opcode::st:
            s += "st";
            break;
        case Opcode::loop:
            s += "loop";
            break;
        case Opcode::loop_pip:
            s += "loop.pip";
            break;
        case Opcode::nop:
            s += "nop";
            break;
        case Opcode::movr:
        case Opcode::movi:
        case Opcode::movp:
            s += "mov";
            break;
        }
        s += " ";
        switch (op) {
        case Opcode::add:
        case Opcode::sub:
        case Opcode::mulu:
            s += "x" + std::to_string(dest) + ", ";
            s += "x" + std::to_string(op_a) + ", ";
            s += "x" + std::to_string(op_b);
            break;
        case Opcode::addi:
            s += "x" + std::to_string(dest) + ", ";
            s += "x" + std::to_string(op_a) + ", ";
            s += std::to_string(imm);
            break;
        case Opcode::ld:
        case Opcode::st:
            s += "x" + std::to_string(dest) + ", ";
            s += std::to_string(imm);
            s += "(x" + std::to_string(op_a) + ")";
            break;
        case Opcode::loop:
        case Opcode::loop_pip:
            s += std::to_string(imm);
            break;
        case Opcode::nop:
            break;
        case Opcode::movr:
            s += "x" + std::to_string(dest) + ", ";
            s += "p" + std::to_string(op_a);
            break;
        case Opcode::movi:
            if (dest == lc_id) {
                s += "LC";
            } else if (dest == ec_id) {
                s += "EC";
            } else {
                s += "x" + std::to_string(dest);
            }
            s += ", " + std::to_string(imm);
            break;
        case Opcode::movp:
            s += "p" + std::to_string(dest) + ", ";
            s += std::to_string(imm);
            break;
        }
        return s;
    }
};

struct VLIWProgram {
  std::vector<Instruction> alu0_instructions;
  std::vector<Instruction> alu1_instructions;
  std::vector<Instruction> mult_instructions;
  std::vector<Instruction> mem_instructions;
  std::vector<Instruction> branch_instructions;
};

typedef std::vector<Instruction> Program;

#endif //COMMON_H
