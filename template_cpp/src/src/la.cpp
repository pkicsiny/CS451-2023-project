
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
#include <algorithm>
#include "assert.h"
#include "parser.hpp"

#include <arpa/inet.h>  // hotnl etc.

#include "utils.hpp"
#include "perfect_link.hpp"

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;

extern unsigned int n_procs;  // urb, num_processes / 2
extern std::vector<std::string> proposed_vec;

void LatticeAgreement::try_decide(std::vector<std::string> proposed_vec){

  // if i get a single nack it means my proposal has changed: broadcast it
  if (nack_cout>0){
    apn++;
    ack_count = 0;
    nack_count = 0;
    pl.do_broadcast=true;
  }

  // need ack from at least half of processes (excluding myself bc from myself I automatically get my proposed_vec)
  if (ack_count>0.5*static_cast<float>(n_procs)){  // checks the ack_cout of the current c_idx only
    logger_p2p.log_decide(proposed_vec, 0)  // decide proposed_set = log to output file
    init_consensus_vars();  // move to next consensus
  }
}

void LatticeAgreement::init_new_consensus(){
  ack_count = 0;
  nack_count = 0;
  apn++;
  c_idx++;

  // take next line from config as proposed_vec
}
