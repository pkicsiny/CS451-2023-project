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

#define MAX_LOG_PERIOD 1
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2

LatticeAgreement::LatticeAgreement(){
  this->apn=1;
  this->c_idx=1;
  for (uint32_t i = 1; i <= n_procs; ++i) {
      this->ack_count[i] = false;
      this->nack_count[i] = false;
  }

  //this->ack_count=0;
  //this->nack_count=0;
}

void LatticeAgreement::try_decide(std::vector<std::string> proposed_vec, bool& do_broadcast, Logger& logger_p2p){


  int num_acks = static_cast<int>(std::count_if(ack_count.begin(), ack_count.end(),
        [](const auto& key_value) {return key_value.second == true;}));
  int num_nacks = static_cast<int>(std::count_if(nack_count.begin(), nack_count.end(),
        [](const auto& key_value) {return key_value.second == true;}));
  std::cout << "[try_decide] num_acks : " << num_acks << ", num_nacks: " << num_nacks << std::endl;

  // if i get a single nack it means my proposal has changed: broadcast it
  if (num_nacks>0){
    std::cout << "got a nack. rebroadcasting my updated proposal, incrementing apn" << std::endl;
    apn++;
    for (uint32_t i = 1; i <= n_procs; ++i) {
        this->ack_count[i] = false;
        this->nack_count[i] = false;
    }
    //ack_count = 0;
    //nack_count = 0;
    do_broadcast=true;
  }

  // need ack from at least half of processes (excluding myself bc from myself I automatically get my proposed_vec)
  // 3 procs: i need 1 ack (+me), 4 procs: i need 2 acks (+me)
  if (num_acks>=floor(0.5*static_cast<float>(n_procs))){  // checks the ack_cout of the current c_idx only
    std::cout << "got enough acks, moving to new consensus and logging decision" << std::endl;
    init_new_consensus(do_broadcast);  // move to next consensus

    // TODO: this doesnt get logged at last consensus, also check sigterm logger
    if(do_broadcast){logger_p2p.log_decide(proposed_vec, 0);}  // decide proposed_set = log to output file
  }
}

void LatticeAgreement::init_new_consensus(bool& do_broadcast){
  //this->ack_count = 0;
  //this->nack_count = 0;
  if (this->c_idx < this->NUM_PROPOSALS){
    for (uint32_t i = 1; i <= n_procs; ++i) {
        this->ack_count[i] = false;
        this->nack_count[i] = false;
    }
    this->apn=1;
    this->c_idx++;
    do_broadcast=true;
    std::cout << "Initting c_idx: "<< this->c_idx << ", apn: "<< this->apn << std::endl;  
  }else{std::cout << "Finished with all decisions." << std::endl;}
}
