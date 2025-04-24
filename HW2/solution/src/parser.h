#ifndef PARSER_H
#define PARSER_H



#include "common.h"

class Parser {
public:
    std::vector<Instruction> parse_program(
        const std::vector<std::string>& program
    );
};



#endif //PARSER_H
