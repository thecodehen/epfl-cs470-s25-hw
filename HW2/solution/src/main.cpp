#include "common.h"
#include "json.hpp"
#include "parser.h"

#include <fstream>
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

  // open output file
  std::string output_file_name {argv[2]};
  std::ofstream output_file(output_file_name);
  if (!output_file.is_open()) {
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
    std::vector<Instruction> v = parser.parse_program(data.value());
    for (const auto& instr : v) {
        std::cout << instr.to_string() << '\n';
    }

  // close files
  input_file.close();
  output_file.close();

  return 0;
}