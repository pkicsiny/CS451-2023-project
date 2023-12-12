#pragma once

#include <chrono>
#include <iostream>
#include <thread>

#include "parser.hpp"
#include "hello.h"
#include <signal.h>

// I load these

#include <fstream>
#include <string>
#include <typeinfo>
#include <arpa/inet.h> // hotnl etc.
#include <sys/socket.h>
#include <unistd.h>
#include "assert.h"
#include <map>
#include <limits>
#include <errno.h>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "utils.hpp"
#include "perfect_link.hpp"

#define MAX_LOG_PERIOD 1
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern std::vector<int> next_vec;  // fifo

class LatticeAgreement {
  public:
    int c_idx;  // consensus index, starts from 1
    int NUM_PROPOSALS;
    int apn;  // active proposal number, starts from 1
    std::map<int, bool> ack_count;
    std::map<int, bool> nack_count;
//    PerfectLink* pl(int, int, std::vector<Parser::Host>);

    LatticeAgreement ();
    void init_new_consensus(bool&);
    void try_decide(std::vector<std::string>, bool&, Logger&);
};
