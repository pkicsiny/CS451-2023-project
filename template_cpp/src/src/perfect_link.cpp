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

PerfectLink::PerfectLink(int pid){
  my_pid = pid;
}

void PerfectLink::send(MessageList& msg_list, Logger& logger_p2p, int socket_fd, sockaddr_in to_addr){

      int w = 0;
      while((!msg_list.msg_list.empty()) & (w<WINDOW_SIZE)){  
        std::cout << "Number of messages in list to send: " << msg_list.msg_list.size() << " =========================" << std::endl;

        // fill up packet with max 8 messages, or until there are msgs left 
        std::vector<char> msg_packet;
        int msg_idx = 0;
        while((msg_idx < MAX_PACKET_SIZE) && !(msg_list.msg_list.empty())){
          EncodeMessage(msg_list.msg_list[0], msg_packet, msg_idx);
          msg_idx += 1;

          // log boradcast event of single message
          LogMessage lm;
          lm.m = msg_list.msg_list[0];
          lm.sender_pid = my_pid;
          lm.msg_type = 'b';
          logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
          logger_p2p.lm_idx++;
          if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
            logger_p2p.log_lm_buffer();
          }
          logger_p2p.msg_pending_for_ack.push_back(msg_list.msg_list[0]);
          msg_list.msg_list.erase(msg_list.msg_list.begin());

          // if msg list empty dont exit but fill up with next chunk if there are rmng msg
          if ((msg_list.msg_list.empty()) & (msg_list.msg_remaining!=0)){
            msg_list.refill();
          }
        }

        size_t packet_size = msg_packet.size();  // byte size, since sizeof(char)=1
        int64_t r_send_msg_packet = sendto(socket_fd, msg_packet.data(), packet_size, 0,
            reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
        if (r_send_msg_packet<0){
          std::cout << "Send failed with error: " << strerror(errno) << std::endl;        
        }else{
          //std::cout << "[SEND] sent packet with bytes: " << r_send_msg_packet << std::endl;
        }
        w++;
      } // end while send window
} // end send()


void PerfectLink::recv_ack(Logger& logger_p2p, int socket_fd, sockaddr_in from_addr){

      socklen_t sizeof_from_addr = sizeof(from_addr);

      while(!(logger_p2p.msg_pending_for_ack.empty())){
        //std::cout << "Listening to acks...\n==========================" << std::endl;

        prev_size = logger_p2p.msg_pending_for_ack.size();

        // this is blocking so it listens indefinitely; make it nonblocking by setting a timeout

        int64_t r_recv_ack_packet = recvfrom(socket_fd, ack_buf, sizeof(ack_buf), 0,
            reinterpret_cast<struct sockaddr *>(&from_addr), &sizeof_from_addr);  // returns length of incoming message
        if (r_recv_ack_packet < 0) {
          //std::cout << "[TIMEOUT] recvfrom timed out or no more incoming data: " << strerror(errno) << std::endl;
          break; // terminate while loop i.e. no more ack to receive
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
            logger_p2p.msg_pending_for_ack.erase(std::remove(logger_p2p.msg_pending_for_ack.begin(), logger_p2p.msg_pending_for_ack.end(), ack_msg), logger_p2p.msg_pending_for_ack.end());

          }  // end for ack_packet
        }  // end if (ack_recv < 0)

        //std::cout << "[STATUS] msg_list size: " << msg_list.msg_list.size() << " pending_list size: " << logger_p2p.msg_pending_for_ack.size() << " Num. msg acked in 10 us window: " << prev_size - logger_p2p.msg_pending_for_ack.size() << std::endl;
      }  // end while recv_ack
} // end recv_ack()


void PerfectLink::resend(Logger& logger_p2p, int socket_fd, sockaddr_in to_addr){
      if(!(logger_p2p.msg_pending_for_ack.empty())){
        std::cout << "Resending unacked messages\n======================= " << std::endl;

        std::vector<char> resend_packet;
        int msg_idx = 0;
        for (Message msg : logger_p2p.msg_pending_for_ack){
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


void PerfectLink::recv(Logger& logger_p2p, int socket_fd, sockaddr_in from_addr){

      char recv_buf[1024]; // buffer for messages in bytes
      std::vector<char> ack_packet;  // byte array for ack messages
      socklen_t sizeof_from_addr = sizeof(from_addr);

      // blocking recv
      int64_t r_recv_msg_packet = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0,
          reinterpret_cast<struct sockaddr *>(&from_addr), &sizeof_from_addr);  // returns length of incoming message
      if (r_recv_msg_packet < 0) {
        std::cout << "Receive failed with error" << std::endl;
      }else if (r_recv_msg_packet != 0) {
        //std::cout << "[RECV] received bytes: " << r_recv_msg_packet << std::endl;

        recv_buf[r_recv_msg_packet] = '\0'; //end of line to truncate junk
        std::vector<char> recv_packet(recv_buf, recv_buf + r_recv_msg_packet);

        //for (auto val : recv_packet) printf("%d ", val);
        //std::cout <<  "..." << std::endl;

        size_t offset = 0;
        std::vector<Message> msg_recv;
        while (offset < recv_packet.size()) {
          msg_recv.push_back(DecodeMessage(recv_packet.data(), offset));
        }

        int sender_port = ntohs(from_addr.sin_port); 
        int sender_pid = port_pid_dict[sender_port];

        // upon successful receive trigger event send ack once, ack is "pid sn msg"
        ack_packet.clear();
        for (Message msg: msg_recv){

          /*------------------*/
          // build ack packet //
          /*------------------*/

          Ack ack(sender_pid, msg.sn, msg.msg);
          EncodeAck(ack, ack_packet, sender_pid);
          //std::cout << "Encoding ack: " << msg.msg << " pid: " << sender_pid<<  " in packet. Num. elements in packet: " << ack_packet.size() << std::endl;

          /*---------*/
          // deliver //
          /*---------*/

          // pid is not in dict i.e. this is the first msg from proc pid
          if (pid_recv_dict.find(sender_pid) == pid_recv_dict.end()) {
            pid_recv_dict[sender_pid].insert(msg.msg);
            //logger_p2p.ss << 'd' << ' ' << sender_pid << ' ' << msg.msg << '\n';
            LogMessage lm;
            lm.m = msg;
            lm.sender_pid = sender_pid;
            lm.msg_type = 'd';
            logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
            logger_p2p.lm_idx++;
            if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
              logger_p2p.log_lm_buffer();
            }                                              

          // pid is already in dict, if msg is duplicate, do not store in log
          } else {

            // if this is true msg_buf is not yet in dict[pid]
            //if (std::find(pid_recv_dict[sender_pid].begin(), pid_recv_dict[sender_pid].end(), msg.msg) == pid_recv_dict[sender_pid].end()){
            if (pid_recv_dict[sender_pid].find(msg.msg) == pid_recv_dict[sender_pid].end()){
              // msg is not yet in dict so log it
              pid_recv_dict[sender_pid].insert(msg.msg);
              //logger_p2p.ss << 'd' << ' ' << sender_pid << ' ' << msg.msg << '\n';
              LogMessage lm;
              lm.m = msg;
              lm.sender_pid = sender_pid;
              lm.msg_type = 'd';
              logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
              logger_p2p.lm_idx++;
              if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
                logger_p2p.log_lm_buffer();
              }

            } // end if
          } // end if
        } // end for packet

        /*-----------------*/
        // send ack packet //
        /*-----------------*/

        size_t ack_packet_size = ack_packet.size();  // byte size, since sizeof(char)=1
        int64_t r_send_ack_packet = sendto(socket_fd, ack_packet.data(), ack_packet_size, 0,
            reinterpret_cast<struct sockaddr *>(&from_addr), sizeof(from_addr));  // returns number of characters sent
        if (r_send_ack_packet < 0) {
            std::cout << "Sending ack message failed with error: " << strerror(errno) << std::endl;
        }else{
          //std::cout << "[SEND] sent ack packet with bytes: " << r_send_ack_packet << std::endl;
          //for (auto val : ack_packet) printf("%d ", val);
          //std::cout << "..." << std::endl;
        }
      } // if (msg_recv < 0)
} // end recv()
