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

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_dict;


extern std::unordered_set<std::string> pid_send_dict;
extern std::vector<Parser::Host> hosts;
extern std::vector<int> next;  // fifo

class Message {
  public:
    int b_pid;  // pid of original broadcaster
    int sn;  // sequence number
    std::string msg;  // actual message string  
    int is_ack;  // 0 for no, 1 for yes

    Message();
    Message(int, int, std::string, int);

    // compare 2 messages
    bool operator==(const Message &m) const {return ((b_pid == m.b_pid) && (sn == m.sn) && (msg == m.msg));}
    bool operator<(const Message &m) const {return sn < m.sn;}

    // compare a string to this message, this is used to remove acked from pending
    bool operator==(const std::string ack_msg) const {return ((msg == ack_msg) && (msg == ack_msg));}
};
extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_dict;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern unsigned int n_procs;  // urb, num_processes / 2
//extern std::map<int64_t, std::vector<Message>> recv_pending_map;
//extern std::map<int64_t, std::vector<Message>> delivered_map;
extern std::map<int64_t, std::vector<Message>> recv_pending_map;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;

struct MessageList{
  std::vector<Message> msg_list;
  int sn_idx;
  int msg_remaining;

  // this is there so that I can send MAX_INT wo filling up the RAM
  void refill(int, int);
};

// for assignment full msg size is: 4+x+4=12 8+x bytes
// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message&, std::vector<char>&, int);
Message DecodeMessage(const char*, size_t &);

class Ack {
  public:
    int pid;  // process id of sender
    int b_pid;  // pid of original broadcaster
    int sn;  // sequence number
    std::string msg;  // actual message string  

    Ack();
    Ack(int, int, std::string);

    // compare 2 ack messages
    bool operator==(const Ack &a) const {return ((b_pid == a.b_pid) && (sn == a.sn) && (msg == a.msg) && (pid == a.pid));}
    bool operator<(const Ack &a) const {return sn < a.sn;}

    // compare an ack message with a message
    bool operator==(const Message &m) const {return ((sn == m.sn) && (msg == m.msg));}
};

struct LogMessage {
  char msg_type;
  int sender_pid;
  Message m; 
};

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;
    int my_pid;
    std::map<int, std::map<int, std::vector<Message>>> relay_map; 

    Logger ();
    Logger (const char*, int);

    int lm_idx;
    LogMessage* lm_buffer;
    bool new_ack=false;

    void print_pending();
    void log_lm_buffer();
    void log_broadcast(Message);
    void log_deliver(Message, int);
    void add_to_ack_seen(Message, int, int);
};

