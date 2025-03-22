#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include "json.hpp"
#include "processor_state.h"

using json = nlohmann::json;

std::optional<json> read_json(std::ifstream& file) {
  json data = json::parse(file);
  return data;
}

void write_json(std::ofstream& file, const json& data) {
  file << data.dump(4) << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input file> <output file>" << std::endl;
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

  // write output file
  processor_state state;
  write_json(output_file, state.to_json());

  // close files
  input_file.close();
  output_file.close();

  return 0;
}