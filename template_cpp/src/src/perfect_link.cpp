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
#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h> 
#include "assert.h"
#define _XOPEN_SOURCE_EXTENDED 1

#include <map>
#include <limits>
#include <errno.h>
#include <unordered_set>

#include "utils.hpp"
#include "perfect_link.hpp"

extern std::map<int, int> port_pid_dict;
extern std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;
extern std::vector<Parser::Host> hosts;

PerfectLink::PerfectLink(int pid){
  my_pid = pid;
}

void PerfectLink::send(MessageList& msg_list, Logger& logger_p2p, int socket_fd, sockaddr_in to_addr, bool log_broadcast){

  int to_port = ntohs(to_addr.sin_port); 
  int to_pid = port_pid_dict[to_port];

  int w = 0;

  while((!msg_list.msg_list.empty()) & (w<WINDOW_SIZE)){  
    std::cout << "Number of messages in list to send to " << to_pid <<": " << msg_list.msg_list.size() << " =========================" << std::endl;

    // fill up packet with max 8 messages, or until there are msgs left 
    std::vector<char> msg_packet;
    int msg_idx = 0;
    int is_ack = 0;
    uint32_t is_ack_ser = htonl(is_ack);
    msg_packet.insert(msg_packet.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes

    // msg_packet: [is_ack, msg_1, msg_2, ..., msg_8]
    while((msg_idx < MAX_PACKET_SIZE) && !(msg_list.msg_list.empty())){
      EncodeMessage(msg_list.msg_list[0], msg_packet, msg_idx);
      msg_idx += 1;
      //std::cout << "Encoding message: " << msg_list.msg_list[0].msg << " in packet at index " << msg_idx << ". Num. elements in packet: " << msg_packet.size() << std::endl;

      // log boradcast event of single message
      if (log_broadcast){
        LogMessage lm;
        lm.m = msg_list.msg_list[0];
        lm.sender_pid = my_pid;  // original broadcaster pid
        lm.msg_type = 'b';
        logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
        logger_p2p.lm_idx++;
        if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
          logger_p2p.log_lm_buffer();
        }
      }

      // acks from to_pid
      logger_p2p.msg_pending_for_ack[to_pid].push_back(msg_list.msg_list[0]);
      msg_list.msg_list.erase(msg_list.msg_list.begin());

      // if msg list empty dont exit but fill up with next chunk if there are rmng msg
      if ((msg_list.msg_list.empty()) & (msg_list.msg_remaining!=0)){
        msg_list.refill(my_pid, 0);
      }
    }
    size_t packet_size = msg_packet.size();  // byte size, since sizeof(char)=1
    int64_t r_send_msg_packet = sendto(socket_fd, msg_packet.data(), packet_size, 0,
        reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
    if (r_send_msg_packet<0){
      std::cout << "[send::ERROR] Send failed with error: " << strerror(errno) << std::endl;        
    }else{
      std::cout << "[send::SEND] sent packet with bytes: " << r_send_msg_packet << std::endl;
    }
    w++;
  } // end while send window
} // end send()


void PerfectLink::recv_ack(Logger& logger_p2p, int socket_fd){

      // sender address
      sockaddr_in from_addr;
      socklen_t sizeof_from_addr = sizeof(from_addr);

      int sender_port = ntohs(from_addr.sin_port); 
      int sender_pid = port_pid_dict[sender_port];

      while(!(logger_p2p.msg_pending_for_ack[sender_pid].empty())){
        std::cout << "Listening to acks from pid: "<< sender_pid << "...\n==========================" << std::endl;

        prev_size = logger_p2p.msg_pending_for_ack[sender_pid].size();

        // this is blocking so it listens indefinitely; make it nonblocking by setting a timeout

        int64_t r_recv_ack_packet = recvfrom(socket_fd, ack_buf, sizeof(ack_buf), 0,
            reinterpret_cast<struct sockaddr *>(&from_addr), &sizeof_from_addr);  // returns length of incoming message
        if (r_recv_ack_packet < 0) {
          //std::cout << "[TIMEOUT] recvfrom timed out or no more incoming data: " << strerror(errno) << std::endl;
          break; // terminate while loop i.e. no more ack to receive

        // process a single received ack
        }else if (r_recv_ack_packet != 0) {
          //std::cout << "[RECV] received bytes: " << r_recv_ack_packet << std::endl;

          // process acked packet //
          ack_buf[r_recv_ack_packet] = '\0'; //end of line to truncate junk
          std::vector<char> ack_packet(ack_buf, ack_buf + r_recv_ack_packet);

          //for (auto val : ack_packet) printf("%d ", val);
          //std::cout <<  "..." << std::endl;

          // decode packed and store in ack_vec
          size_t offset = 0;
          std::vector<Ack> ack_vec;
          while (offset < ack_packet.size()) {
            ack_vec.push_back(DecodeAck(ack_packet.data(), offset));
          }

          int serv_port = ntohs(from_addr.sin_port);

          for (Ack ack : ack_vec){
            int ack_pid = ack.pid;
            std::string ack_msg = ack.msg;

            //std::cout << "Removing ack message: " << ack_msg << " from pid: "<< ack_pid << " from pending." << std::endl;    

            // msg_pending_for_ack is unique; only get back ack of msgs that were sent from this process
            // if ack_msg is not in pending then pending remains unmodified
            logger_p2p.msg_pending_for_ack[sender_pid].erase(std::remove(logger_p2p.msg_pending_for_ack[sender_pid].begin(), logger_p2p.msg_pending_for_ack[sender_pid].end(), ack_msg), logger_p2p.msg_pending_for_ack[sender_pid].end());

          }  // end for ack_packet
        }  // end if (ack_recv < 0)

        //std::cout << "[STATUS] msg_list size: " << msg_list.msg_list.size() << " pending_list size: " << logger_p2p.msg_pending_for_ack.size() << " Num. msg acked in 10 us window: " << prev_size - logger_p2p.msg_pending_for_ack.size() << std::endl;
      }  // end while recv_ack
} // end recv_ack()


void PerfectLink::resend(Logger& logger_p2p, int socket_fd, sockaddr_in to_addr){

      int to_port = ntohs(to_addr.sin_port); 
      int to_pid = port_pid_dict[to_port];

      if(!(logger_p2p.msg_pending_for_ack[to_pid].empty())){
        std::cout << "Resending unacked messages\n======================= " << std::endl;

        std::vector<char> resend_packet;
        int msg_idx = 0;
        for (Message msg : logger_p2p.msg_pending_for_ack[to_pid]){
          EncodeMessage(msg, resend_packet, msg_idx);
          msg_idx += 1;

          // send packet when full //
          if (msg_idx == MAX_PACKET_SIZE){
            size_t packet_size = resend_packet.size();  // byte size, since sizeof(char)=1
            //std::cout << "Resending packet with size: "<< packet_size << std::endl;
            int64_t r_resend_msg_packet = sendto(socket_fd, resend_packet.data(), packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
              std::cout << "Send failed with error: " << strerror(errno) << std::endl;
            }else{
              //std::cout << "[SEND] resent packet with bytes: " << r_resend_msg_packet << std::endl;
              //for (auto val : resend_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            } 
            msg_idx = 0; // reset packet index to overwrite with new msg
            resend_packet.clear();
          } // end if send

        } // end for messages

        // mod MAX_PACKET_SIZE //
        if ((msg_idx>0) & (msg_idx<MAX_PACKET_SIZE)){  // this is a packet smaller than MAX_PACKET_SIZE messages
            size_t packet_size = resend_packet.size();  // byte size, since sizeof(char)=1
            int64_t r_resend_msg_packet = sendto(socket_fd, resend_packet.data(), packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
              std::cout << "Send failed with error: " << strerror(errno) << std::endl;
            }else{
              //std::cout << "[SEND] resent packet with bytes: " << r_resend_msg_packet << std::endl;
              //for (auto val : resend_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            } 
        } // end if residuals
      } // end if resend unacked
} // end resend()


void PerfectLink::recv(Logger& logger_p2p, int socket_fd){

  // address of sender
  sockaddr_in from_addr;
  socklen_t sizeof_from_addr = sizeof(from_addr);

  while(true){
      char recv_buf[1024]; // buffer for messages in bytes
      std::vector<char> ack_packet;  // byte array for ack messages

      // blocking recv
      int64_t r_recv_msg_packet = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0,
          reinterpret_cast<struct sockaddr *>(&from_addr), &sizeof_from_addr);  // returns length of incoming message

      if (r_recv_msg_packet < 0) {
        std::cout << "[recv::TIMEOUT] recvfrom timed out or no more incoming data: " << strerror(errno) << std::endl;
        break;
      // decode single msg received
      }else if (r_recv_msg_packet != 0) {
        int sender_port = ntohs(from_addr.sin_port); 
        int sender_pid = port_pid_dict[sender_port];

        std::cout << "[recv::RECV] received bytes: " << r_recv_msg_packet << " from pid: " << sender_pid << std::endl;

        recv_buf[r_recv_msg_packet] = '\0'; //end of line to truncate junk
        std::vector<char> recv_packet(recv_buf, recv_buf + r_recv_msg_packet);  // put received data into vector

        for (auto val : recv_packet) printf("%d ", val);
        std::cout <<  "..." << std::endl;

        size_t offset = 0;
        int is_ack;
        // decocde is_ack
        std::memcpy(&(is_ack), recv_packet.data() + offset, sizeof(uint32_t));
        is_ack = ntohl(is_ack);
        offset += sizeof(uint32_t);

        std::cout << "offset: " << offset << ", is_ack: " << is_ack  << std::endl;

        // decode messages
        std::vector<Message> msg_recv;
        while (offset < recv_packet.size()) {
          msg_recv.push_back(DecodeMessage(recv_packet.data(), offset));
        }

        std::cout << "spearated and decoded messages" << std::endl;
        // upon successful receive trigger event send ack once, ack is "b_pid sn msg 1"
        ack_packet.clear();
        int packet_idx = 0;

        // msg is an ack
        if (is_ack==1){
          for (Message msg: msg_recv){
            int b_pid = msg.b_pid;
            std::string ack_msg = msg.msg;
            std::cout << "Removing ack message: " << ack_msg << " originally sent from pid: "<< b_pid << " from pending." << std::endl;

            // msg_pending_for_ack is unique; get back ack of msgs sent from all processes
            // if ack_msg is not in pending then pending_for_ack remains unmodified
            logger_p2p.msg_pending_for_ack[b_pid].erase(std::remove(logger_p2p.msg_pending_for_ack[b_pid].begin(), logger_p2p.msg_pending_for_ack[b_pid].end(), ack_msg), logger_p2p.msg_pending_for_ack[b_pid].end());
          }

        // this msg is not an ack i.e. it has been broadcast or relayed. It comes from msg.b_pid.
        }else{

          uint32_t is_ack_ser = htonl(1);
          ack_packet.insert(ack_packet.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes

          for (Message msg: msg_recv){

            /*------------------*/
            // build ack packet //
            /*------------------*/

            Message ack(msg.b_pid, msg.sn, msg.msg, 1);
            EncodeMessage(ack, ack_packet, packet_idx);
            packet_idx++;
            //std::cout << "Encoding ack: " << msg.msg << " sent from pid: " << sender_pid<<  " in packet. Num. elements in packet: " << ack_packet.size() << ". This ack packet will be sent to all processes" << std::endl;  

            /*---------*/
            // deliver //
            /*---------*/
  
            // pid is not in dict i.e. this is the first msg from proc pid
            int b_pid = msg.b_pid;
            if (pid_recv_dict.find(b_pid) == pid_recv_dict.end()) {
              pid_recv_dict[b_pid].insert(msg.msg);
              //logger_p2p.ss << 'd' << ' ' << b_pid << ' ' << msg.msg << '\n';
              LogMessage lm;
              lm.m = msg;
              lm.sender_pid = b_pid;
              lm.msg_type = 'd';
              logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
              logger_p2p.lm_idx++;
              if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
                logger_p2p.log_lm_buffer();
              }                                              
  
            // pid is already in dict, if msg is duplicate, do not store in log
            } else {
  
              // if this is true msg_buf is not yet in dict[pid]
              if (pid_recv_dict[b_pid].find(msg.msg) == pid_recv_dict[b_pid].end()){
                // msg is not yet in dict so log it
                pid_recv_dict[b_pid].insert(msg.msg);
                //logger_p2p.ss << 'd' << ' ' << b_pid << ' ' << msg.msg << '\n';
                LogMessage lm;
                lm.m = msg;
                lm.sender_pid = b_pid;
                lm.msg_type = 'd';
                logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
                logger_p2p.lm_idx++;
                if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
                  logger_p2p.log_lm_buffer();
                }
  
              } // end if
            } // end if
          } // end for packet
  
          /*---------------------------------------*/
          // send ack packet to all processes once //
          /*---------------------------------------*/

          size_t ack_packet_size = ack_packet.size();  // byte size, since sizeof(char)=1
          struct sockaddr_in to_addr;  // other process address

          for (auto &host : hosts) {
            to_addr.sin_family = AF_INET;
            to_addr.sin_addr.s_addr = inet_addr(host.ipReadable().c_str()); //INADDR_ANY;  
            to_addr.sin_port = htons(host.port);  // port of receiving process
            std::cout << "[recv::SEND] Send to: pid: " << host.id << ", (machine readable) IP: " << to_addr.sin_addr.s_addr << ", (human readable) port: " << to_addr.sin_port <<  std::endl;
            int64_t r_send_ack_packet = sendto(socket_fd, ack_packet.data(), ack_packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr));  // returns number of characters sent
            if (r_send_ack_packet < 0) {
                std::cout << "[recv::ERROR] Sending ack message failed with error: " << strerror(errno) << std::endl;
            }else{
              std::cout << "[recv::SEND] sent ack packet with bytes: " << r_send_ack_packet << std::endl;
              //for (auto val : ack_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            }
          }
        }  // end if ack==1
      }  // end if recv successful
    } // end while
} // end recv()
