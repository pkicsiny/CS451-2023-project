#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <map>
#include <unordered_set>
#include "parser.hpp"
#include "utils.hpp"
#include "perfect_link.hpp"

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;


extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;
extern std::vector<int> next_vec;  // fifo

class LatticeAgreement {
  public:
    int c_idx;  // consensus index
    int apn;  // active proposal number
    int ack_count;
    int nack_count;
    Logger* logger_p2p;
    PerfectLink* pl(int, int, std::vector<Parser::Host>);

    LatticeAgreement ();
    void init_next_consensus();
    void try_decide(std::vector<std::string>);
};
