#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include "json.hpp"
#include "simulator.h"

using json = nlohmann::json;

std::optional<json> read_json(std::istream& file) {
  json data = json::parse(file);
  return data;
}

void write_json(std::ostream& os, const json& data) {
  os << data.dump(4) << std::endl;
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
  if (!data) {
    std::cerr << "Failed to read JSON data from file: " << input_file_name << std::endl;
    return 1;
  }

  // create simulator
  simulator sim(data.value());

  // step through the simulator
  json::array_t states;
  states.push_back(sim.get_json_state());
  uint32_t i = 0;
  while (sim.can_step()) {
    std::cout << "---------- cycle " << i++ << " ----------" << std::endl;
    sim.step();
    states.push_back(sim.get_json_state());
  }

  // write output file
  write_json(output_file, states);

  // close files
  input_file.close();
  output_file.close();

  return 0;
}