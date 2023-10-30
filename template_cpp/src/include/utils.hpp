#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

#define MAX_MSG_LENGTH_BYTES = 255;  // 256th is 0 terminator

class Message {
  public:
    int sn;  // sequence number
    std::string msg;  // actual message string  

    Message(){}
    Message(int sequencenumber, std::string message) {
      sn = sequencenumber;
      msg = message;
    }
};

// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer);
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer) {

    // serialize the sequence number to network byte order
    uint32_t sn_ser = htonl(msg.sn);  // 4 bytes encoding seq. num.
    
    // serialize the message string
    const char* msg_ser = msg.msg.data();  // 1 byte per character, pointer to byte repr. of msg
    size_t msg_size = msg.msg.size();
    uint32_t msg_ser_size = htonl(static_cast<uint32_t>(msg_size));  // 4 bytes encoding msg length

    // order of serialized Message: [len_msg, msg, sn]
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&msg_ser_size), reinterpret_cast<char*>(&msg_ser_size) + sizeof(uint32_t));
    msg_buffer.insert(msg_buffer.end(), msg_ser, msg_ser + msg_size);
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&sn_ser), reinterpret_cast<char*>(&sn_ser) + sizeof(uint32_t));
}

Message DecodeMessage(const char* msg_buffer, size_t &offset);
Message DecodeMessage(const char* msg_buffer, size_t &offset) {  // offset=0 for first call
    Message msg;

    // Read the length of the struct's data
    uint32_t msg_ser_size;
    std::memcpy(&msg_ser_size, msg_buffer + offset, sizeof(uint32_t));
    size_t msg_size = ntohl(msg_ser_size);
    offset += sizeof(uint32_t);

    // Deserialize the string
    msg.msg.assign(msg_buffer + offset, msg_size);
    offset += msg_size;

    // Deserialize the int
    std::memcpy(&(msg.sn), msg_buffer, sizeof(uint32_t));
    msg.sn = ntohl(msg.sn);
    offset += sizeof(uint32_t);

    return msg;
}





class AckMessage {
  public:
    int pid;  // process id of sender
    int sn;  // sequence number
    std::string msg;  // actual message string  

    AckMessage(){}
    AckMessage(int processid, int sequencenumber, std::string message) {
      pid = processid;
      sn = sequencenumber;
      msg = message;
    }
};

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;

    Logger ();
    Logger (const char*);

    int log_delivery(){
    
      // log into
      std::ofstream output_file;
      output_file.open(output_path, std::ios_base::app);
    
      if (output_file.is_open()){
        output_file << ss.str();
        output_file.close();
        return 0;
      }else{
        std::cout << "Could not open output file: " << output_path << std::endl;
        return -1;
      }
    }

};

#define MAX_PACKET_SIZE 8

struct MsgPacket{
  int msg_idx;  
  Message m_vec[MAX_PACKET_SIZE];
};

struct AckPacket{
  int ack_idx;
  AckMessage a_vec[MAX_PACKET_SIZE];
};

Logger::Logger(){
}

Logger::Logger(const char* op){
  output_path = op;
}
