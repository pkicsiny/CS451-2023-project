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
#include <mutex>

#include "utils.hpp"

#define MAX_LOG_PERIOD 10
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, bool> delivered_map;

void EncodeMetadata(std::vector<char>& msg_buffer, int is_ack, int c_idx, int apn, int b_pid){

    uint32_t is_ack_ser = htonl(is_ack);
    uint32_t c_idx_ser = htonl(c_idx);
    uint32_t apn_ser = htonl(apn);
    uint32_t b_pid_ser = htonl(b_pid);

    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&c_idx_ser), reinterpret_cast<char*>(&c_idx_ser) + sizeof(uint32_t));  // 4 bytes
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&apn_ser), reinterpret_cast<char*>(&apn_ser) + sizeof(uint32_t));  // 4 bytes
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&b_pid_ser), reinterpret_cast<char*>(&b_pid_ser) + sizeof(uint32_t));  // 4 bytes


}


void EncodeProposal(std::vector<std::string> proposal, std::vector<char>& msg_buffer) {

    size_t num_elements = proposal.size();
    uint32_t num_elements_ser = htonl(static_cast<uint32_t>(num_elements));
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&num_elements_ser), reinterpret_cast<char*>(&num_elements_ser) + sizeof(uint32_t));  // 4 bytes


    // order of serialized Message: [n, p1_size, p1, p2_size, p2, ... pn_size, pn]
    for (std::string proposal_i : proposal){
      const char* proposal_i_ser = proposal_i.data();  // 1 byte per character, pointer to byte repr. of msg
      size_t proposal_i_size = proposal_i.size();
      uint32_t proposal_i_ser_size = htonl(static_cast<uint32_t>(proposal_i_size));  // 4 bytes encoding msg length

      msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&proposal_i_ser_size), reinterpret_cast<char*>(&proposal_i_ser_size) + sizeof(uint32_t));  // 4 bytes
      msg_buffer.insert(msg_buffer.end(), proposal_i_ser, proposal_i_ser + proposal_i_size);  // 1 byte
    }
}

void DecodeMetadata(const char* msg_buffer, int& is_ack, int& c_idx, int& apn, int& b_pid, size_t& offset){

    // decocde is_ack
    uint32_t is_ack_ser;
    std::memcpy(&(is_ack_ser), msg_buffer + offset, sizeof(uint32_t));
    is_ack = ntohl(is_ack_ser);
    offset += sizeof(uint32_t);

    // decocde c_idx
    uint32_t c_idx_ser;
    std::memcpy(&(c_idx_ser), msg_buffer + offset, sizeof(uint32_t));
    c_idx = ntohl(c_idx_ser);
    offset += sizeof(uint32_t);

    // decocde apn
    uint32_t apn_ser;
    std::memcpy(&apn_ser, msg_buffer + offset, sizeof(uint32_t));
    apn = ntohl(apn_ser);
    offset += sizeof(uint32_t);

    // decocde b_pid
    uint32_t b_pid_ser;
    std::memcpy(&(b_pid_ser), msg_buffer + offset, sizeof(uint32_t));
    b_pid = ntohl(b_pid_ser);
    offset += sizeof(uint32_t);

}


std::vector<std::string> DecodeProposal(const char* msg_buffer, size_t &offset) {  // offset=0 for first call

    std::vector<std::string> decoded_proposal;

    // get number of elements in proposal
    uint32_t num_elements_ser;
    std::memcpy(&num_elements_ser, msg_buffer + offset, sizeof(uint32_t));
    size_t num_elements = ntohl(num_elements_ser);
    offset += sizeof(uint32_t);

   // std::cout << "num_elements in recved proposal: "<< num_elements << std::endl;

    for (size_t n=0; n<num_elements; n++){

      // proposal_i_size
      uint32_t proposal_i_ser_size;
      std::memcpy(&(proposal_i_ser_size), msg_buffer + offset, sizeof(uint32_t));
      size_t proposal_i_size = ntohl(proposal_i_ser_size);
      offset += sizeof(uint32_t);
     // std::cout << "size of next int: " << proposal_i_size << std::endl;

      // decode proposal_i
      std::string proposal_i;
      proposal_i.assign(msg_buffer + offset, proposal_i_size);
      offset += proposal_i_size;
      decoded_proposal.push_back(proposal_i);
      //std::cout << "next proposal int: "<<proposal_i << std::endl;
    }

    return decoded_proposal;
}


void Logger::log_ld_buffer(int call_mode){

  // check number of lines in output file
  int num_lines = 0;
  std::ifstream output_file_line_count(output_path);
  std::string unused_line;
  while (std::getline(output_file_line_count, unused_line)){
    ++num_lines;
  }
  //std::cout << "There are " << num_lines << " decisions logged in the file." << std::endl;
  if (output_file_line_count.is_open()){output_file_line_count.close();}

  int log_start_idx = num_lines % MAX_LOG_PERIOD;
  //std::cout << "current buffer logged until index " << log_start_idx << std::endl;


  std::fstream output_file;
  //std::cout << "log_ld_buffer call mode: " << call_mode << ", file is open: "<< output_file.is_open() << std::endl;

  output_file.open(output_path, std::ios_base::app);

  if (output_file.is_open()){
    for(int i = log_start_idx; i < ld_idx; i++) {
      std::stringstream ss; // stringstream containing a full log line
      ss << ld_buffer[i].line;
      //if ((i==5) && (call_mode==0)){std::cout << "sleep" << std::endl; output_file.close(); sleep(100000);}
      //std::cout << "sigterm ld_idx: "<< i << ", logging the line: " << ld_buffer[i].line << ", ss: " << ss.str()<< std::endl;
      output_file << ss.str();
    }
    ld_idx = 0; // reset pointer in log buffer
  }else{
    std::cout << "Could not open output file: " << output_path << std::endl;
  }

/*
  output_file.open(output_path, std::ios_base::in | std::ios_base::app);
  bool do_log;
  int last_ld_idx = -1;
  if (output_file.is_open()){
    for(int i = 0; i < ld_idx; i++) {
      //std::cout << "loop " << i << "/" << ld_idx << std::endl;
      do_log = true; // for each msg try to log by default
      std::stringstream ss; // stringstream containing a full log line
      ss << ld_buffer[i].line;
      //if ((i==5) && (call_mode==0)){std::cout << "sleep" << std::endl; output_file.close(); sleep(100000);}
      do_log = check_dupes(do_log, output_file, ss, call_mode, last_ld_idx, i); // check if msg already in logfile (relevant after sigterm/int)
      if (do_log){
        std::cout << "sigterm ld_idx: "<< i << ", logging the line: " << ld_buffer[i].line << ", ss: " << ss.str()<< std::endl;
        output_file << ss.str();
      }
    }
    ld_idx = 0; // reset pointer in log buffer
  }else{
    std::cout << "Could not open output file: " << output_path << std::endl;
  }
*/

  output_file.close();
  //std::cout << "log_ld_buffer properly finished" << std::endl;
}

Logger::Logger(){
  ld_idx = 0;
}

Logger::Logger(const char* op, int pid){
  output_path = op;
  my_pid = pid;
  ld_idx = 0;
}


// when sigterm issued while logging: part of log buffer is logged, sigterm log call relogs full chunk, some can be dupes
bool Logger::check_dupes(bool& do_log, std::fstream& output_file, std::stringstream& desired_line, int call_mode, int& last_ld_idx, int i){
    //std::cout << "check_dupes " << i << std::endl;
  // this is called only upon sigterm/sigint
  if ((call_mode==1) && (i==last_ld_idx+1)){
    std::string cur_line;
    output_file.seekg(0, std::ios::beg);  // move get (read) pointer to start

    int line_counter = 0;
    while (std::getline(output_file, cur_line)) {
      if (std::cin.good()) {cur_line += '\n';} // if there should be nl add nl (relevant at last line)

      // check if line already in log file
      if (cur_line == desired_line.str()) {
        do_log = false;
        last_ld_idx = i;
        //std::cout << "ld_idx: " << i << " is a dupe with line (starts at 0) "<<line_counter << std::endl;
        break;  // exit while loop, dont log this line
      }
      line_counter++;
    }
    //std::cout << "check_dupes set write pointer to eof" << std::endl;
    output_file.clear();  // loop could have reached end of file, reset errorbit
    output_file.seekp(0, std::ios_base::end); // set put pointer to end of file
  }
  return do_log;
}


void Logger::log_decide(std::vector<std::string>& proposed_vec, int c_idx, int call_mode){

  // concat proposal integers into one line
  LogDecision ld;
  ld.line = std::accumulate(std::begin(proposed_vec), std::end(proposed_vec), std::string(),
        [](const std::string& a, const std::string& b) -> std::string { return a + (a.length() > 0 ? " " : "") + b;}
    ) + "\n";
  //std::cout << "ld_idx: "<< ld_idx << ", logging the line: " << ld.line << std::endl;
  ld_buffer[ld_idx] = ld;
  ld_idx++;

  // free up memory proposed_vec[c_idx] of consensus c_idx
  delivered_map[c_idx] = true;
  proposed_vec.clear();

  // ld_idx goes up to maxlogperiod-1 and then ++ and this triggers
  if(ld_idx == MAX_LOG_PERIOD){log_ld_buffer(call_mode);}
}

void read_single_line(const char* config_path, int c_idx, std::map<int, std::vector<std::string>>& proposed_vec){

  std::ifstream config_file;
  config_file.open(config_path);

  if (config_file.is_open()){
    std::string l_line;

    //skip N lines
    for(int i = 0; i < c_idx; i++)
      std::getline(config_file, l_line);

    // get line number c_idx (starts from 1)
    std::getline(config_file, l_line);
    std::istringstream iss(l_line);

    // split set values and push into vector
    while(iss){
    std::string l_header_i;
      iss >> l_header_i;
      if (!l_header_i.empty()) {
        proposed_vec[c_idx].push_back(l_header_i);  // c_idx-1 was emptied in log_decide
      }
    }
  }

  config_file.close();


  //std::cout << "Read config line "<< c_idx << ", current proposed_vec: ";
  //for (const auto& element : proposed_vec[c_idx]) {
  //  std::cout << element << ", ";
 // }
 // std::cout << std::endl;

}
