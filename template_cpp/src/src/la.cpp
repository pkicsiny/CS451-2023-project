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
#include "la.hpp"

#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, bool> delivered_map;

LatticeAgreement::LatticeAgreement(){
  this->apn[1] = 1;
  this->c_idx=1;
  for (uint32_t i = 1; i <= n_procs; ++i) {
      this->ack_count[i] = false;
      this->nack_count[i] = false;
  }
}

void LatticeAgreement::try_decide(std::vector<std::string>& proposed_vec, bool& do_broadcast, Logger& logger_p2p){
//  std::cout << "=========================Attempt to decide=========================" << std::endl;

  int required_acks = static_cast<int>(floor(0.5*static_cast<float>(n_procs)));

  int num_acks = static_cast<int>(std::count_if(ack_count.begin(), ack_count.end(),
        [](const auto& key_value) {return key_value.second == true;}));
  int num_nacks = static_cast<int>(std::count_if(nack_count.begin(), nack_count.end(),
        [](const auto& key_value) {return key_value.second == true;}));
  //std::cout << "[try_decide] Waiting for " << required_acks << " acks. num_acks : " << num_acks << ", num_nacks: " << num_nacks << ", proposed_vec: ";
//  for (const auto& element : proposed_vec) {
//    std::cout << element << ", ";
//  }
//  std::cout << std::endl;

  // if i get a single nack it means my proposal has changed: broadcast it
  if (num_nacks>0){
    //std::cout << "[try_decide] Got a nack. Rebroadcasting my updated proposal, incrementing apn" << std::endl;
    this->apn[this->c_idx]++;
    for (uint32_t i = 1; i <= n_procs; ++i) {
        this->ack_count[i] = false;
        this->nack_count[i] = false;
    }
    do_broadcast=true;
  }

  // need ack from at least half of processes (excluding myself bc from myself I automatically get my proposed_vec)
  // 3 procs: i need 1 ack (+me), 4 procs: i need 2 acks (+me)
  if (num_acks>=required_acks){  // checks the ack_cout of the current c_idx only
    //std::cout << "[try_decide] Got enough acks, moving to new consensus and logging decision" << std::endl;

    // deliver only if not yet delivered, after delivery it becomes true
    if (delivered_map.find(this->c_idx) == delivered_map.end()){
      logger_p2p.log_decide(proposed_vec, this->c_idx, 0);
    }  // decide proposed_set = log to output file

    init_new_consensus(do_broadcast, logger_p2p);  // move to next consensus
  }
}

void LatticeAgreement::init_new_consensus(bool& do_broadcast, Logger& logger_p2p){

  // upon initting a new round I can erase proposal_vec from the previous round
  //std::cout << "Free up memory of c_idx: " << this->c_idx << std::endl;
  logger_p2p.resend_map.erase(this->c_idx);

  if (this->c_idx < this->NUM_PROPOSALS){
    for (uint32_t i = 1; i <= n_procs; ++i) {
        this->ack_count[i] = false;
        this->nack_count[i] = false;
    }
    this->c_idx++;
    this->apn[this->c_idx]=1;
    do_broadcast=true;
    //std::cout << "=========================Init new consensus with c_idx: "<< this->c_idx << ", apn: "<< this->apn[this->c_idx]<< "=========================" << std::endl;
  }else{
    //logger_p2p.resend_map.erase(this->c_idx);
    //std::cout << "Finished with all decisions." << std::endl; 
  }
}
