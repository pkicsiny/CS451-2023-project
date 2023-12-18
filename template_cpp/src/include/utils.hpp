#pragma once

#include <chrono>
#include <iostream>
#include <thread>

#include "parser.hpp"
#include "hello.h"
#include <signal.h>

// I load these

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <map>
#include <unordered_set>
#include <algorithm>
#include "assert.h"
#include <numeric>
#include <arpa/inet.h>  // hotnl etc.

#define MAX_LOG_PERIOD 10
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, std::vector<std::string>> delivered_map;

void EncodeMetadata(std::vector<char>&, int, int, int, int);
void EncodeProposal(std::vector<std::string>, std::vector<char>&);
void DecodeMetadata(const char*, int&, int&, int&, int&, size_t&);
std::vector<std::string> DecodeProposal(const char*, size_t&);

struct LogDecision {
  std::string line; 
};

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;
    int my_pid;
    std::map<int, std::map<int, std::map<int, std::vector<char>>>> resend_map; 

    Logger ();
    Logger (const char*, int);

    int ld_idx;
    LogDecision* ld_buffer;
    bool new_ack=false;

    void print_pending();
    void log_ld_buffer(int);
    bool check_dupes(bool&, std::fstream&, std::stringstream&, int, int&, int);
    void log_decide(std::vector<std::string>, int, int);
    void init_new_consensus();
};

