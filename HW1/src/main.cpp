#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include "json.hpp"
using json = nlohmann::json;

std::optional<json> parse_input_file(const std::string &input_file) {
  std::ifstream file(input_file);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << input_file << std::endl;
    return std::nullopt;
  }
  json data = json::parse(file);
  file.close();
  return data;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input file>" << std::endl;
    return 1;
  }

  std::string input_file {argv[1]};
  std::optional<json> data = parse_input_file(input_file);
  if (!data) {
    return 1;
  }
  std::cout << data.value().dump(4) << std::endl;

  return 0;
}