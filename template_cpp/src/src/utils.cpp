
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

#include <arpa/inet.h>  // hotnl etc.

#include "utils.hpp"

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 10
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::map<int64_t, std::vector<Message>> recv_pending_map;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;
extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;

extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_map;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::vector<int> next_vec;  // fifo

Message::Message(){}
Message::Message(int broadcaster_pid, int sequencenumber, std::string message, int is_ack_msg) {
      b_pid = broadcaster_pid;
      sn = sequencenumber;
      msg = message;
      is_ack = is_ack_msg;
}

// this is there so that I can send MAX_INT wo filling up the RAM
void MessageList::refill(int b_pid, int is_ack){
    int msg_list_chunk_size = std::min(MAX_MSG_LIST_SIZE, msg_remaining);
    for(auto ii=0; ii<msg_list_chunk_size; ii++){
      msg_list.push_back(Message(b_pid, sn_idx, std::to_string(sn_idx+1), is_ack));  // sequencing starts from 0
      sn_idx++;  // goes up to NUM_MSG
      msg_remaining--;  // goes down to 0
    }
    //std::cout << "Message list refilled with " << msg_list.size() << " messages." << std::endl;
  }

// for assignment full msg size is: 4+x+4=12 8+x bytes
// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer, int packet_idx) {

    uint32_t sn_ser = htonl(msg.sn);  // 4 bytes encoding seq. num. in network byte order; seq. num is max. MAX_INT so 4 bytes needed
    uint32_t b_pid_ser = htonl(msg.b_pid);  // 4 bytes encoding original sender pid in network byte order; b_pid num is max. 128
    const char* msg_ser = msg.msg.data();  // 1 byte per character, pointer to byte repr. of msg
    size_t msg_size = msg.msg.size();
    uint32_t msg_ser_size = htonl(static_cast<uint32_t>(msg_size));  // 4 bytes encoding msg length

//    std::cout << "encoding msg: " << msg.msg << std::endl;

    // order of serialized Message: [len_msg, msg, sn, b_pid, is_ack]
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&msg_ser_size), reinterpret_cast<char*>(&msg_ser_size) + sizeof(uint32_t));  // 4 bytes
//    std::cout << "after encoding size: " << msg_buffer.size() << ", " << msg.msg.size() << std::endl;
    msg_buffer.insert(msg_buffer.end(), msg_ser, msg_ser + msg_size);  // 1 byte
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&sn_ser), reinterpret_cast<char*>(&sn_ser) + sizeof(uint32_t));  // 4 bytes
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&b_pid_ser), reinterpret_cast<char*>(&b_pid_ser) + sizeof(uint32_t));  // 4 bytes
//    std::cout << "msg_buffer after encoding sn: " << msg_buffer.size() << " bytes" << std::endl;
}

Message DecodeMessage(const char* msg_buffer, size_t &offset) {  // offset=0 for first call
    Message msg;

    // get length of msg
    uint32_t msg_ser_size;
    std::memcpy(&msg_ser_size, msg_buffer + offset, sizeof(uint32_t));
    size_t msg_size = ntohl(msg_ser_size);
    offset += sizeof(uint32_t);
//    std::cout << "after decoding size: " << msg_size << ", offset: " << offset << std::endl;

    // decode msg
    msg.msg.assign(msg_buffer + offset, msg_size);
    offset += msg_size;
//    std::cout << "after decoding msg: " << msg.msg << ", offset: " << offset << std::endl;

    // decocde sn
    std::memcpy(&(msg.sn), msg_buffer + offset, sizeof(uint32_t));
    msg.sn = ntohl(msg.sn);
    offset += sizeof(uint32_t);
//    std::cout << "after decoding sn: " << msg.sn << ", offset: " << offset << std::endl;

    // decocde b_pid
    std::memcpy(&(msg.b_pid), msg_buffer + offset, sizeof(uint32_t));
    msg.b_pid = ntohl(msg.b_pid);
    offset += sizeof(uint32_t);
//    std::cout << "after decoding b_pid: " << msg.b_pid << ", offset: " << offset << std::endl;

    return msg;
}

void Logger::print_pending(){
          for (auto const& x : relay_map){
            for (auto const& y : relay_map[x.first]){
            std::cout << "still pending msgs broadcast from pid (" << x.first << ") to pid ("<< y.first <<"): " << y.second.size() << std::endl;
            }
          }
}

void Logger::log_lm_buffer(int call_mode){
  //std::cout << "Call mode: " << call_mode << std::endl;
  // log into
  std::fstream output_file;
  output_file.open(output_path, std::ios_base::in | std::ios_base::app);
  bool do_log;
  int last_lm_idx = -1;

  if (output_file.is_open()){
    for(int i = 0; i < lm_idx; i++) {
      do_log = true; // for each msg try to log by default
      std::stringstream ss; // stringstream containing a full log line
      if(lm_buffer[i].msg_type == 'b'){
        ss << "b " << lm_buffer[i].m.msg << std::endl;
        //std::cout << ss.str();
      }else{
        ss << "d " << lm_buffer[i].sender_pid << ' ' << lm_buffer[i].m.msg << std::endl;
        //std::cout << ss.str();
      }
      do_log = check_dupes(do_log, output_file, ss, call_mode, last_lm_idx, i); // check if msg already in logfile (relevant after sigterm/int)
      if (do_log){output_file << ss.str();}
    }
    lm_idx = 0; // reset pointer in log buffer
    output_file.close();
  }else{
    std::cout << "Could not open output file: " << output_path << std::endl;
  }
}

Logger::Logger(){
}

Logger::Logger(const char* op, int pid){
  output_path = op;
  my_pid = pid;
}

bool Logger::check_dupes(bool& do_log, std::fstream& output_file, std::stringstream& desired_line, int call_mode, int& last_lm_idx, int i){
  if ((call_mode==1) && (i==last_lm_idx+1)){
    std::string cur_line;
//    std::cout << "looking for: " << desired_line.str() << std::endl;
    output_file.seekg(0, std::ios::beg);  // move get (read) pointer to start
    while (std::getline(output_file, cur_line)) {
      if (std::cin.good()) {cur_line += '\n';} // if there should be nl and nl (relevant at last line)
      bool equal = cur_line == desired_line.str(); 
  //    std::cout << "success: " << cur_line << "==" << desired_line.str() << "equal?: "<< equal << std::endl;
      if (cur_line == desired_line.str()) {
    //    std::cout << "found " << desired_line.str() << " already in log. Not logging this." << std::endl;
        do_log = false;
        last_lm_idx = i;
        break;  // exit while loop, dont log this line
      }
    }

    output_file.clear();  // loop could have reached end of file, reset errorbit
    output_file.seekp(0, std::ios_base::end); // set put pointer to end of file
  }
  return do_log;
}


void Logger::log_deliver(Message msg, int is_ack, int call_mode){

//  std::cout << "ack_seen_map[msg].size: " << ack_seen_map[msg].size() << ", n_procs / 2: " << 0.5*n_procs << std::endl;
  int b_pid = msg.b_pid;


//  if (recv_pending_map[b_pid].find(msg.msg) == recv_pending_map[b_pid].end()){

  // if not yet delivered and not in pending (ie new msg, could be from any sender), add to pendong. [] creates b_pid key
  if (delivered_map[b_pid].find(msg.msg) == delivered_map[b_pid].end()){
    if (std::find(recv_pending_map[b_pid].begin(), recv_pending_map[b_pid].end(), msg) == recv_pending_map[b_pid].end()){
       recv_pending_map[b_pid].push_back(msg);
      // std::cout << "adding b" << b_pid << ' ' << msg.msg << " to relay" << std::endl;
  
      // this takes care of relay: i resend msgs that were sent to me (broadcasts, relays)
      // only if not yet in pending, add to relay list; one msg from a b_pid will be added to relay list only once
      // msg broadcast from myself is added to pending in send
      if ((b_pid != my_pid) && (is_ack==0)){

        // these are only the msgs broadcast from someone else
        for (auto & relay_to_host : hosts_vec){
          int relay_to_pid = port_pid_map[relay_to_host.port];

          // i dont relay to myself
          if (relay_to_pid != my_pid){
            relay_map[b_pid][relay_to_pid].push_back(msg);
          }
        }
      }
    }
  }

  // if it wasnt deliverable last time it wont unless a new processs seen msg
  if (new_ack){
    new_ack = false;
    // attempt delivery: for all msg that is already in pending
    std::sort(recv_pending_map[b_pid].begin(), recv_pending_map[b_pid].end());
    auto it = recv_pending_map[b_pid].begin();
    while(it != recv_pending_map[b_pid].end()) {
   
        // urb: if msg is not yet delivered and seen by more than half of procs
        if ((delivered_map[b_pid].find((*it).msg) == delivered_map[b_pid].end()) &&
          (static_cast<float>(ack_seen_map[b_pid][(*it).sn].size()) > 0.5*n_procs)){
  
          // fifo: if this is the next sn
          if((*it).sn == next_vec[b_pid-1]) {
            next_vec[b_pid-1]++;
            //std::cout << "d " << (*it).b_pid << ' ' << (*it).msg << std::endl;
            delivered_map[b_pid].insert((*it).msg);
            LogMessage lm;
            lm.m = *it;
            lm.sender_pid = b_pid;
            lm.msg_type = 'd';
            lm_buffer[lm_idx] = lm;
            lm_idx++;
            if(lm_idx == MAX_LOG_PERIOD){log_lm_buffer(call_mode);} // 100 msgs from different pids, but pid ordered
            it = recv_pending_map[b_pid].erase(it);
            }else{it++;}  // end fifo deliver
        }else{it++;} // end urb deliver
    } // end while
  }
}

void Logger::log_broadcast(Message msg, int call_mode){

  // if this is true msg_buf is not yet in dict[pid]
  if (pid_send_uset.find(msg.msg) == pid_send_uset.end()){

    // msg is not yet in dict so log it
    pid_send_uset.insert(msg.msg);

    // log boradcast event of single message
    LogMessage lm;
    lm.m = msg;
    lm.sender_pid = my_pid;  // original broadcaster pid
    lm.msg_type = 'b';
    lm_buffer[lm_idx] = lm;
    lm_idx++;
    if(lm_idx == MAX_LOG_PERIOD){log_lm_buffer(call_mode);}
  } // end if
}

void Logger::add_to_ack_seen(Message msg, int sender_pid, int is_ack){

//   std::cout << "Msgs contained in ack_seen_map before:" << std::endl;
//   for (auto &mes : ack_seen_map){
//      std::cout << "(b" << mes.first << ' ';
//      for (auto &mes_sn: mes.second){
//        std::cout << "sn " << mes_sn.first << "): seen by " << mes_sn.second.size() << " processes." << std::endl;
//     }
//   }
//   std::cout << std::endl;

  // [] creates keys if doesnt already exist
  if (ack_seen_map[msg.b_pid][msg.sn].find(sender_pid) == ack_seen_map[msg.b_pid][msg.sn].end()){
    ack_seen_map[msg.b_pid][msg.sn].insert(sender_pid);
    new_ack = true;
    //std::cout << "msg b" << msg.b_pid << ' ' << msg.msg << " is now seen by pid: "<< sender_pid << std::endl;
    // attempt urb delivery
    //log_deliver(msg, is_ack);
   }
//if(is_ack==1){std::cout << std::endl;}
//     std::cout << "Msgs contained in ack_seen_map after:" << std::endl;

//     for (auto &mes : ack_seen_map){
//       std::cout << "(b" << mes.first << ' ';
//       for (auto &mes_sn: mes.second){
//         std::cout << "sn " << mes_sn.first << "): seen by " << mes_sn.second.size() << " processes." << std::endl;
//     }
//   }
//   std::cout << std::endl;
}


