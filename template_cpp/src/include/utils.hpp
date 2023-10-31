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

// for assignment full msg size is: 4+4+4+4=16 bytes
// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer, int packet_idx);
void EncodeMessage(const Message& msg, std::vector<char>& msg_buffer, int packet_idx) {

    // serialize the sequence number to network byte order
    uint32_t sn_ser = htonl(msg.sn);  // 4 bytes encoding seq. num.; seq. num is max. MAX_INT so 4 bytes needed
//    uint32_t packet_idx_ser = htonl(packet_idx);  // 1 byte encoding packet idx (max. 8)

    // serialize the message string
    const char* msg_ser = msg.msg.data();  // 1 byte per character, pointer to byte repr. of msg
    size_t msg_size = msg.msg.size();
    uint32_t msg_ser_size = htonl(static_cast<uint32_t>(msg_size));  // 4 bytes encoding msg length

    //std::cout << "encoding msg: " << msg.msg << std::endl;

    // order of serialized Message: [len_msg, msg, sn, packet_idx]
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&msg_ser_size), reinterpret_cast<char*>(&msg_ser_size) + sizeof(uint32_t));  // 4 bytes
//    std::cout << "msg_buffer after encoding msg_size: " << msg_buffer.size() << " bytes" << std::endl;
    msg_buffer.insert(msg_buffer.end(), msg_ser, msg_ser + msg_size);  // 1 byte
//    std::cout << "msg_buffer after encoding msg: " << msg_buffer.size() << " bytes" << std::endl;
    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&sn_ser), reinterpret_cast<char*>(&sn_ser) + sizeof(uint32_t));  // 4 bytes
//    msg_buffer.insert(msg_buffer.end(), reinterpret_cast<char*>(&packet_idx_ser), reinterpret_cast<char*>(&packet_idx_ser) + sizeof(uint32_t));

//    std::cout << "msg_buffer after encoding sn: " << msg_buffer.size() << " bytes" << std::endl;
}

Message DecodeMessage(const char* msg_buffer, size_t &offset);
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

    // decocde packet_idx
    //uint32_t packet_idx_ser;
    //std::memcpy(&(packet_idx_ser), msg_buffer, sizeof(uint32_t));
    //int packet_idx = ntohl(packet_idx_ser);
    //offset += sizeof(uint32_t);

    return msg;
}


class Ack {
  public:
    int pid;  // process id of sender
    int sn;  // sequence number
    std::string msg;  // actual message string  

    Ack(){}
    Ack(int processid, int sequencenumber, std::string message) {
      pid = processid;
      sn = sequencenumber;
      msg = message;
    }
};

// serialize a single message into msg_buffer which contains all msg in packet
void EncodeAck(const Ack& ack, std::vector<char>& ack_buffer, int sender_pid);
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

Ack DecodeAck(const char* ack_buffer, size_t &offset);
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
  Ack a_vec[MAX_PACKET_SIZE];
};

Logger::Logger(){
}

Logger::Logger(const char* op){
  output_path = op;
}
