#include "parser.h"

#include <iostream>
#include <sstream>

std::vector<Instruction> Parser::parse_program(
    const std::vector<std::string>& program
) {
    std::vector<Instruction> instructions;
    for (const auto& line : program) {
        Instruction instr;

        // parse the line and fill the instruction struct
        std::stringstream ss(line);
        std::string op, operand1, operand2;
        ss >> op;
        if (op == "add") {
            instr.op = Opcode::add;
        } else if (op == "addi") {
            instr.op = Opcode::addi;
        } else if (op == "sub") {
            instr.op = Opcode::sub;
        } else if (op == "mulu") {
            instr.op = Opcode::mulu;
        } else if (op == "ld") {
            instr.op = Opcode::ld;
        } else if (op == "st") {
            instr.op = Opcode::st;
        } else if (op == "loop") {
            instr.op = Opcode::loop;
        } else if (op == "loop.pip") {
            instr.op = Opcode::loop_pip;
        } else if (op == "nop") {
            instr.op = Opcode::nop;
        } else if (op == "mov") {
            ss >> operand1 >> operand2;
            if (operand1.at(0) == 'p') {
                instr.op = Opcode::movp;
            } else if (operand2.at(0) == 'x') {
                instr.op = Opcode::movr;
            } else {
                instr.op = Opcode::movi;
            }
        } else {
            std::cerr << "Unknown opcode: " << op << std::endl;
        }

        // parse the first operand
        switch(instr.op) {
        case Opcode::add:
        case Opcode::sub:
        case Opcode::mulu: {
            std::string dest, op_a, op_b;
            ss >> dest >> op_a >> op_b;
            instr.dest = std::stoi(dest.substr(1));
            instr.op_a = std::stoi(op_a.substr(1));
            instr.op_b = std::stoi(op_b.substr(1));
            break;
        }
        case Opcode::addi: {
            std::string dest, op_a, imm;
            ss >> dest >> op_a >> imm;
            instr.dest = std::stoi(dest.substr(1));
            instr.op_a = std::stoi(op_a.substr(1));
            instr.imm = std::stoll(imm);
            break;
        }
        case Opcode::ld:
        case Opcode::st: {
            std::string dest, imm_addr;
            ss >> dest >> imm_addr;
            instr.dest = std::stoi(dest.substr(1));

            // parse immediate and the register storing the address
            instr.imm = std::stoll(imm_addr, nullptr, 0);
            std::string::size_type pos {imm_addr.find('(')};
            if (pos == std::string::npos) {
                std::cerr << "Error: Invalid address format in instruction: " \
                    << line << '\n';
                break;
            }
            try {
                instr.op_a = std::stoull(imm_addr.substr(pos + 2));
            } catch (std::invalid_argument const& ex) {
                std::cerr << "Error: Invalid argument: " << ex.what() \
                   << " for line " << line << '\n';
            }
            break;
        }
        case Opcode::loop:
        case Opcode::loop_pip: {
            std::string loop_start;
            ss >> loop_start;
            instr.imm = std::stoll(loop_start, nullptr, 0);
            break;
        }
        case Opcode::movr:
            instr.dest = std::stoi(operand1.substr(1));
            instr.op_a = std::stoi(operand2.substr(1));
            break;
        case Opcode::movi:
            if (operand1.at(0) == 'L' || operand1.at(0) == 'E') {
                instr.dest = operand1.at(0) == 'L' ? lc_id : ec_id;
            } else {
                instr.dest = std::stoi(operand1.substr(1));
            }
            instr.imm = std::stoll(operand2, nullptr, 0);
            break;
        case Opcode::movp:
            instr.dest = std::stoi(operand1.substr(1));
            if (operand2 == "true") {
                instr.imm = 1;
            } else {
                instr.imm = 0;
            }
            break;
        case Opcode::nop:
            // nothing to do. added for better understanding
            break;
        default:
            break;
        }

        instructions.push_back(instr);
    }
    return instructions;
}
