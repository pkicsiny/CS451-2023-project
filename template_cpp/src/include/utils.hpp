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

extern std::map<int, int> port_pid_map;


extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;
extern std::vector<int> next_vec;  // fifo

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

    // compare msg to int sequence number
    bool operator==(const int sn_other) const {return (sn == sn_other);}
    bool operator<(const int sn_other) const {return sn < sn_other;}
};
extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_map;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int64_t, std::map<int, Message>> pending_msg_map;
extern std::map<int64_t, std::unordered_set<int>> pending_sn_uset;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;
extern std::vector<Message> proposal;

struct LogMessage {
  std::string line; 
};

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;
    int my_pid;
    std::map<int, std::map<int, std::map<int, std::vector<char>>>< resend_map; 

    Logger ();
    Logger (const char*, int);

    int ld_idx;
    LogMessage* ld_buffer;
    bool new_ack=false;

    void print_pending();
    void log_ld_buffer(int);
    bool check_dupes(bool&, std::fstream&, std::stringstream&, int, int&, int);
    void log_decide(std::vector<std::string>, int);
    void init_new_consensus();
};


class LatticeAgreement{
  public:
    int c_idx;  // consensus index
    int apn;  // active proposal number
    int ack_count;
    int nack_count;
    Logger* logger_p2p;
    PerfectLink* pl;

    LatticeAgreement (int);
    void init_next_consensus();
    void try_decide(std::vector<std::string>);
}

LatticeAgreement::LatticeAgreement(){
  this->ack_count = 0;
  this->nack_count = 0;
  this->apn = 1;
  this->c_idx = 1;
}

void LatticeAgreement::try_decide(std::vector<std::string> proposed_vec){

  // if i get a single nack it means my proposal has changed: broadcast it
  if (nack_cout>0){
    apn++;
    ack_count = 0;
    nack_count = 0;
    pl.do_broadcast=true;
  }

  // need ack from at least half of processes (excluding myself bc from myself I automatically get my proposed_vec)
  if (ack_count>0.5*static_cast<float>(n_procs)){  // checks the ack_cout of the current c_idx only
    logger_p2p.log_decide(proposed_vec, 0)  // decide proposed_set = log to output file
    init_consensus_vars();  // move to next consensus
  }
}

void LatticeAgreement::init_new_consensus(){
  ack_count = 0;
  nack_count = 0;
  apn++;
  c_idx++;

  // take next line from config as proposed_vec
}
