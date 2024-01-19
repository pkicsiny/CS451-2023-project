#pragma once

#include <chrono>
#include <iostream>
#include <thread>

#include "parser.hpp"
#include <signal.h>

// I load these

#include <fstream>
#include <string>
#include <typeinfo>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "assert.h"
#include <map>
#include <limits>
#include <errno.h>
#include <unordered_set>
#include <cmath>
#include <set>
#include <mutex>

#include "utils.hpp"

#define _XOPEN_SOURCE_EXTENDED 1

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, std::vector<std::string>> accepted_vec;

class PerfectLink{
  public:
    PerfectLink(int, int, std::vector<Parser::Host>);

    int my_pid;
    int n_procs;
    size_t prev_size;
    std::vector<Parser::Host> hosts_vec;
    bool do_broadcast;
    std::mutex pl_mutex;

    std::vector<char> create_send_packet(std::vector<std::string>, int, int, Logger&, int);

    void broadcast(std::vector<std::string>, Logger&, int, sockaddr_in, int, std::map<int, int>);
    void send(std::vector<char>, sockaddr_in, int, int, int, int);
    void recv(std::vector<std::string>&, Logger&, int, int, std::map<int, int>, std::map<int, bool>&, std::map<int, bool>&);
    void resend(Logger&, int, sockaddr_in, int, std::map<int, int>);
    void send_ack(std::vector<char>, sockaddr_in, int);
};
