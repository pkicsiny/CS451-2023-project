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
#define _XOPEN_SOURCE_EXTENDED 1

#include <map>
#include <limits>
#include <errno.h>
#include <unordered_set>

#include "utils.hpp"

extern std::map<int, int> port_pid_map;
extern std::map<int64_t, std::map<int, Message>> pending_msg_map;
extern std::map<int64_t, std::unordered_set<int>> pending_sn_uset;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;

extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;
extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_map;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::vector<int> next_vec;  // fifo

class PerfectLink{
  public:
    int my_pid;
    size_t prev_size;
    char ack_buf[1024];
    std::vector<bool> lock_send_vec;  // lock_send_vec[pid-1], sliding window, stops sending new msgs to pid until this proc acked all msgs previously sent to pid
    std::vector<int> total_resent;
    std::vector<int> total_ack_sent;
    std::vector<int> total_recv;
    std::vector<int> total_ack_recv;

    PerfectLink(int);

    void send(MessageList&, Logger&, int, sockaddr_in);
    void recv_ack(Logger&, int);
    void resend(Logger&, int, sockaddr_in, int, int);
    void recv(Logger&, int);

};
