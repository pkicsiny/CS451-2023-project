#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#define MAX_LOG_PERIOD 100
#define WINDOW_SIZE 50
#define MAX_MSG_LIST_SIZE 1024 // >0 this is there so that I can send MAX_INT wo filling up the RAM
#define MAX_MSG_LENGTH_BYTES = 255;  // >0 256th is 0 terminator
#define MAX_PACKET_SIZE 8  // fixed by assignment

class Message {
  public:
    int sn;  // sequence number
    std::string msg;  // actual message string  

    Message();
    Message(int, std::string);

    // compare 2 messages
    bool operator==(const Message &m) const {return ((sn == m.sn) && (msg == m.msg));}
    bool operator<(const Message &m) const {return sn < m.sn;}

    // compare a string to the message, this is used to remove acked from pending
    bool operator==(const std::string ack_msg) const {return ((msg == ack_msg) && (msg == ack_msg));}
};


struct MessageList{
  std::vector<Message> msg_list;
  int sn_idx;
  int msg_remaining;

  // this is there so that I can send MAX_INT wo filling up the RAM
  void refill();
};

// for assignment full msg size is: 4+x+4=12 8+x bytes
// serialize a single message into msg_buffer which contains all msg in packet
void EncodeMessage(const Message&, std::vector<char>&, int);
Message DecodeMessage(const char*, size_t &);

class Ack {
  public:
    int pid;  // process id of sender
    int sn;  // sequence number
    std::string msg;  // actual message string  

    Ack();
    Ack(int, int, std::string);

    // compare 2 ack messages
    bool operator==(const Ack &a) const {return ((sn == a.sn) && (msg == a.msg) && (pid == a.pid));}
    bool operator<(const Ack &a) const {return sn < a.sn;}

    // compare an ack message with a message
    bool operator==(const Message &m) const {return ((sn == m.sn) && (msg == m.msg));}
};

// serialize a single message into msg_buffer which contains all msg in packet
void EncodeAck(const Ack& ack, std::vector<char>& ack_buffer, int sender_pid);
Ack DecodeAck(const char* ack_buffer, size_t &offset);

struct LogMessage {
  char msg_type;
  int sender_pid;
  Message m; 
};

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;
    std::vector<Message> msg_pending_for_ack; 

    Logger ();
    Logger (const char*);

    int lm_idx;
    LogMessage* lm_buffer;

    void log_lm_buffer();
};

