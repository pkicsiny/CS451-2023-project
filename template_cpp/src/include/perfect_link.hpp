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

extern std::map<int, int> port_pid_dict;
extern std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;
extern std::unordered_set<std::string> pid_send_dict;
extern std::vector<Parser::Host> hosts;

class PerfectLink{
  public:
    int my_pid;
    size_t prev_size;
    char ack_buf[1024];

    PerfectLink(int);

    void send(MessageList&, Logger&, int, sockaddr_in);
    void recv_ack(Logger&, int);
    void resend(Logger&, int, sockaddr_in, int);
    void recv(Logger&, int);

};
