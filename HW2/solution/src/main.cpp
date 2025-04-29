#include "common.h"
#include "json.hpp"
#include "loop_compiler.h"
#include "loop_pip_compiler.h"
#include "parser.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

using json = nlohmann::json;

std::optional<json> read_json(std::istream& file) {
    json data = json::parse(file);
    return data;
}

void write_json(std::ostream& os, const json& data) {
    os << data.dump(4) << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <input file> <output loop file> <output loop.pip file>" << std::endl;
        return 1;
    }

    // open input file
    std::string input_file_name {argv[1]};
    std::ifstream input_file(input_file_name);
    if (!input_file.is_open()) {
        std::cerr << "Failed to open file: " << input_file_name << std::endl;
        return 1;
    }

    // open output loop file
    std::string output_file_name {argv[2]};
    std::ofstream output_loop_file(output_file_name);
    if (!output_loop_file.is_open()) {
        std::cerr << "Failed to open file: " << output_file_name << std::endl;
        return 1;
    }

    // open output loop.pip file
    output_file_name = argv[3];
    std::ofstream output_loop_pip_file(output_file_name);
    if (!output_loop_pip_file.is_open()) {
        std::cerr << "Failed to open file: " << output_file_name << std::endl;
        return 1;
    }

    // read input file
    std::optional<json> data = read_json(input_file);
    if (!data) {
        std::cerr << "Failed to read JSON data from file: " << input_file_name << std::endl;
        return 1;
    }

    Parser parser;
    Program v = parser.parse_program(data.value());
    for (auto it = v.begin(); it != v.end(); ++it) {
        std::cout << std::setfill('0') << std::setw(5)
            << std::distance(v.begin(), it) << ": " << it->to_string() << '\n';
    }
    LoopCompiler loop_compiler{v};
    VLIWProgram loop_program = loop_compiler.compile();
    write_json(output_loop_file, loop_program.to_json());

    LoopPipCompiler loop_pip_compiler{v};
    VLIWProgram loop_pip_program = loop_pip_compiler.compile();
    write_json(output_loop_pip_file, loop_pip_program.to_json());

    // close files
    input_file.close();
    output_loop_file.close();
    output_loop_pip_file.close();

    return 0;
}