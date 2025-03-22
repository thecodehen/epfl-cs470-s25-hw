#ifndef PROCESSOR_STATE_H
#define PROCESSOR_STATE_H



#include <cstdint>
#include <list>
#include <vector>
#include <deque>
#include <queue>
#include "common.h"
#include "json.hpp"
#include "max_size_queue.hpp"

using json = nlohmann::json;

class processor_state {
public:
  pc_t pc {};
  std::vector<uint64_t> physical_register_file;
  std::deque<std::pair<pc_t, instruction_t>> decoded_pcs;
  pc_t exception_pc {};
  bool exception {};
  std::vector<reg_t> register_map_table;
  std::deque<reg_t> free_list;
  std::vector<bool> busy_bit_table;
  std::deque<active_list_entry_t> active_list;
  std::list<integer_queue_entry_t> integer_queue;

  // non-visible states
  std::vector<std::queue<alu_queue_entry_t>> alu_queues; // similar to register 3
  std::vector<std::queue<alu_result_t>> alu_results; // similar to register 5
  processor_state();
  json to_json() const;
};



#endif //PROCESSOR_STATE_H
