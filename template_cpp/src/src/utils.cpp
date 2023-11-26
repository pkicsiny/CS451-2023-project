
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
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_dict;
extern std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;
extern std::unordered_set<std::string> pid_send_dict;
extern std::vector<Parser::Host> hosts;

extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_dict;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::vector<int> next;  // fifo

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
          for (auto const& x : msg_pending_for_ack){
            for (auto const& y : msg_pending_for_ack[x.first]){
            //std::cout << "still pending msgs broadcast from pid (" << x.first << ") to pid ("<< y.first <<"): " << y.second.size() << std::endl;
            }
          }
}

void Logger::log_lm_buffer(){

      // log into
      std::ofstream output_file;
      output_file.open(output_path, std::ios_base::app);

      if (output_file.is_open()){
        for(int i = 0; i < lm_idx; i++) {
          if(lm_buffer[i].msg_type == 'b'){
            output_file << "b " << lm_buffer[i].m.msg << std::endl;
          }else{
            output_file << "d " << lm_buffer[i].sender_pid << ' ' << lm_buffer[i].m.msg << std::endl;
          }
        }
        lm_idx = 0;
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

void Logger::log_deliver(Message msg, int is_ack){

//  std::cout << "ack_seen_dict[msg].size: " << ack_seen_dict[msg].size() << ", n_procs / 2: " << 0.5*n_procs << std::endl;
  int b_pid = msg.b_pid;

  // [] creates b_pid key
  if (pid_recv_dict[b_pid].find(msg.msg) == pid_recv_dict[b_pid].end()){

    // this takes care of relay: i resend msgs that were sent to me (broadcasts, relays)
    // if msg is already a relayed one i.e. sender_pid =/= b_pid then msg is already in pending so I dont relay relayed

    // msg broadcast from myself is added to pending in send
    if ((b_pid != my_pid) && (is_ack==0)){

      // these are only the msgs broadcast from someone else
      for (auto & relay_to_host : hosts){
        int relay_to_pid = port_pid_dict[relay_to_host.port];

        // i dont relay to myself
        if (relay_to_pid != my_pid){
          msg_pending_for_ack[b_pid][relay_to_pid].push_back(msg);
        }
      }
      print_pending();
    }

    // urb deliver
    if (static_cast<float>(ack_seen_dict[msg.b_pid][msg.sn].size()) > 0.5*n_procs){
      pid_recv_dict[b_pid].insert(msg.msg);
      LogMessage lm;
      lm.m = msg;
      lm.sender_pid = b_pid;
      lm.msg_type = 'd';
      lm_buffer[lm_idx] = lm;
      lm_idx++;
      if(lm_idx == MAX_LOG_PERIOD){log_lm_buffer();}
    } // end urb deliver
 //     else{std::cout << "cannot urb deliver msg (ack=" << is_ack<< "): d " << b_pid << ' ' << msg.msg << std::endl;}

  } // end if
}

void Logger::log_broadcast(Message msg){

  // if this is true msg_buf is not yet in dict[pid]
  if (pid_send_dict.find(msg.msg) == pid_send_dict.end()){

    // msg is not yet in dict so log it
    pid_send_dict.insert(msg.msg);

    // log boradcast event of single message
    LogMessage lm;
    lm.m = msg;
    lm.sender_pid = my_pid;  // original broadcaster pid
    lm.msg_type = 'b';
    lm_buffer[lm_idx] = lm;
    lm_idx++;
    if(lm_idx == MAX_LOG_PERIOD){log_lm_buffer();}
  } // end if
}

void Logger::add_to_ack_seen(Message msg, int sender_pid, int is_ack){

//   std::cout << "Msgs contained in ack_seen_dict before:" << std::endl;
//   for (auto &mes : ack_seen_dict){
//      std::cout << "(b" << mes.first << ' ';
//      for (auto &mes_sn: mes.second){
//        std::cout << "sn " << mes_sn.first << "): seen by " << mes_sn.second.size() << " processes." << std::endl;
//     }
//   }
//   std::cout << std::endl;

  // msg from this b_pid was already seen by some other pid-s, [] creates keys
  if (ack_seen_dict[msg.b_pid][msg.sn].find(sender_pid) == ack_seen_dict[msg.b_pid][msg.sn].end()){
    ack_seen_dict[msg.b_pid][msg.sn].insert(sender_pid);
    // attempt urb delivery
    log_deliver(msg, is_ack);
   }
//if(is_ack==1){std::cout << std::endl;}
//     std::cout << "Msgs contained in ack_seen_dict after:" << std::endl;
//     for (auto &mes : ack_seen_dict){
//       std::cout << "(b" << mes.first << ' ';
//       for (auto &mes_sn: mes.second){
//         std::cout << "sn " << mes_sn.first << "): seen by " << mes_sn.second.size() << " processes." << std::endl;
//     }
//   }
//   std::cout << std::endl;
}
