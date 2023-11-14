
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <arpa/inet.h>  // hotnl etc.

#include "utils.hpp"

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

Message::Message(){}
Message::Message(int sequencenumber, std::string message) {
      sn = sequencenumber;
      msg = message;
}

// this is there so that I can send MAX_INT wo filling up the RAM
void MessageList::refill(){
    int msg_list_chunk_size = std::min(MAX_MSG_LIST_SIZE, msg_remaining);
    for(auto ii=0; ii<msg_list_chunk_size; ii++){
      msg_list.push_back(Message(sn_idx, std::to_string(sn_idx+1)));  // sequencing starts from 0
      sn_idx++;  // goes up to NUM_MSG
      msg_remaining--;  // goes down to 0
    }
    //std::cout << "Message list refilled with " << msg_list.size() << " messages." << std::endl;
  }

// for assignment full msg size is: 4+x+4=12 8+x bytes
// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer, int packet_idx) {

    // serialize the sequence number to network byte order
    uint32_t sn_ser = htonl(msg.sn);  // 4 bytes encoding seq. num.; seq. num is max. MAX_INT so 4 bytes needed

    // serialize the message string
    const char* msg_ser = msg.msg.data();  // 1 byte per character, pointer to byte repr. of msg
    size_t msg_size = msg.msg.size();
    uint32_t msg_ser_size = htonl(static_cast<uint32_t>(msg_size));  // 4 bytes encoding msg length

    //std::cout << "encoding msg: " << msg.msg << std::endl;

    // order of serialized Message: [len_msg, msg, sn]
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&msg_ser_size), reinterpret_cast<char*>(&msg_ser_size) + sizeof(uint32_t));  // 4 bytes
//    std::cout << "msg_buffer after encoding msg_size: " << msg_buffer.size() << " bytes" << std::endl;
    msg_buffer.insert(msg_buffer.end(), msg_ser, msg_ser + msg_size);  // 1 byte
//    std::cout << "msg_buffer after encoding msg: " << msg_buffer.size() << " bytes" << std::endl;
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&sn_ser), reinterpret_cast<char*>(&sn_ser) + sizeof(uint32_t));  // 4 bytes
//    std::cout << "msg_buffer after encoding sn: " << msg_buffer.size() << " bytes" << std::endl;
}

Message DecodeMessage(const char* msg_buffer, size_t &offset) {  // offset=0 for first call
    Message msg;

    // get length of msg
    uint32_t msg_ser_size;
    std::memcpy(&msg_ser_size, msg_buffer + offset, sizeof(uint32_t));
    size_t msg_size = ntohl(msg_ser_size);
    offset += sizeof(uint32_t);

    // decode msg
    msg.msg.assign(msg_buffer + offset, msg_size);
    offset += msg_size;

    // decocde sn
    std::memcpy(&(msg.sn), msg_buffer + offset, sizeof(uint32_t));
    msg.sn = ntohl(msg.sn);
    offset += sizeof(uint32_t);

    return msg;
}

Ack::Ack(){}
Ack::Ack(int processid, int sequencenumber, std::string message) {
      pid = processid;
      sn = sequencenumber;
      msg = message;
}

// serialize a single message into msg_buffer which contains all msg in packet
void EncodeAck(const Ack& ack, std::vector<char>& ack_buffer, int sender_pid) {

    uint32_t sn_ser = htonl(ack.sn);
    uint32_t pid_ser = htonl(sender_pid);
    const char* ack_ser = ack.msg.data();
    size_t ack_size = ack.msg.size();
    uint32_t ack_ser_size = htonl(static_cast<uint32_t>(ack_size));

    //std::cout << "Encode ack: 'ack_size': " << ack_size << ", 'ack.msg': " << ack.msg << ", 'ack.sn':" << ack.sn << ", 'ack.pid':" << ack.pid << std::endl;

    // order of serialized Message: [len_msg, msg, sn, pid]
    ack_buffer.insert(ack_buffer.end(), reinterpret_cast<char*>(&ack_ser_size), reinterpret_cast<char*>(&ack_ser_size) + sizeof(uint32_t));
    ack_buffer.insert(ack_buffer.end(), ack_ser, ack_ser + ack_size);
    ack_buffer.insert(ack_buffer.end(), reinterpret_cast<char*>(&sn_ser), reinterpret_cast<char*>(&sn_ser) + sizeof(uint32_t));
    ack_buffer.insert(ack_buffer.end(), reinterpret_cast<char*>(&pid_ser), reinterpret_cast<char*>(&pid_ser) + sizeof(uint32_t));
}

Ack DecodeAck(const char* ack_buffer, size_t &offset) {  // offset=0 for first call
    Ack ack;

    // get length of msg
    uint32_t ack_ser_size;
    std::memcpy(&ack_ser_size, ack_buffer + offset, sizeof(uint32_t));
    size_t ack_size = ntohl(ack_ser_size);
    offset += sizeof(uint32_t);
    //std::cout << "offset: " << offset << std::endl;

    // decode msg
    ack.msg.assign(ack_buffer + offset, ack_size);
    offset += ack_size;
    //std::cout << "offset: " << offset << std::endl;

    // decocde sn
    std::memcpy(&(ack.sn), ack_buffer + offset, sizeof(uint32_t));
    ack.sn = ntohl(ack.sn);
    offset += sizeof(uint32_t);
    //std::cout << "offset: " << offset << std::endl;

    // decocde sender pid
    uint32_t pid_ser;
    std::memcpy(&(pid_ser), ack_buffer + offset, sizeof(uint32_t));
    ack.pid = ntohl(pid_ser);
    offset += sizeof(uint32_t);

    //std::cout << "Decoded ack: 'ack_size': " << ack_size << ", 'ack.msg': " << ack.msg << ", 'ack.sn':" << ack.sn << ", 'ack.pid':" << ack.pid << std::endl;

    return ack;
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

Logger::Logger(const char* op){
  output_path = op;
}

