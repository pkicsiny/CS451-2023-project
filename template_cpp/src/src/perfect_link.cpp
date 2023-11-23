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
extern std::unordered_set<std::string> pid_send_dict;
extern std::vector<Parser::Host> hosts;

PerfectLink::PerfectLink(int pid){
  my_pid = pid;
}

void PerfectLink::send(MessageList& msg_list, Logger& logger_p2p, int socket_fd, sockaddr_in to_addr){

  int to_port = ntohs(to_addr.sin_port); 
  int to_pid = port_pid_dict[to_port];

  int w = 0;

  while((!msg_list.msg_list.empty()) & (w<WINDOW_SIZE)){  
    std::cout << "=========================Number of messages in list to send to pid (" << to_pid <<"): " << msg_list.msg_list.size() << "=========================" << std::endl;

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
      logger_p2p.log_broadcast(msg_list.msg_list[0]);

      // wait for ack of msg og boradcast from my_pid to to_pid
      logger_p2p.msg_pending_for_ack[my_pid][to_pid].push_back(msg_list.msg_list[0]);
      logger_p2p.print_pending();
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


void PerfectLink::resend(Logger& logger_p2p, int socket_fd, sockaddr_in to_addr, int from_pid){

      int to_port = ntohs(to_addr.sin_port); 
      int to_pid = port_pid_dict[to_port];

      std::cout << "=========================Resending unacked messages og broadcast from pid ("<< from_pid <<") to pid ("<< to_pid <<")=========================" << std::endl;
      logger_p2p.print_pending();

      // new keys initted here with empty vector value
      if(!(logger_p2p.msg_pending_for_ack[from_pid][to_pid].empty())){

        std::vector<char> resend_packet;
        int msg_idx = 0;

        for (Message msg : logger_p2p.msg_pending_for_ack[from_pid][to_pid]){

          // encode is_ack at beginning of packet
          if (msg_idx==0){
            uint32_t is_ack_ser = htonl(0);
            resend_packet.insert(resend_packet.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes
          }

          EncodeMessage(msg, resend_packet, msg_idx);
          msg_idx += 1;
          std::cout << "Encoding message: (" << msg.msg << ", " << msg.b_pid << ")." << std::endl;  


          // send packet when full //
          if (msg_idx == MAX_PACKET_SIZE){
            size_t packet_size = resend_packet.size();  // byte size, since sizeof(char)=1
            //std::cout << "Resending packet with size: "<< packet_size << std::endl;
            int64_t r_resend_msg_packet = sendto(socket_fd, resend_packet.data(), packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
              std::cout << "[resend::SEND] resend to pid ("<< to_pid <<") failed with error: " << strerror(errno) << std::endl;
            }else{
              std::cout << "[resend::SEND] resent packet to pid ("<< to_pid <<") with bytes: " << r_resend_msg_packet << std::endl;
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
              std::cout << "[resend::SEND] resend to pid ("<< to_pid <<") failed with error: " << strerror(errno) << std::endl;
            }else{
              std::cout << "[resend::SEND] resent packet to pid ("<< to_pid <<") with bytes: " << r_resend_msg_packet << std::endl;
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

  std::cout << "=========================Listening for messages to receive=========================" << std::endl;

  while(true){
      char recv_buf[1024]; // buffer for messages in bytes
      std::vector<char> ack_packet;  // byte array for ack messages

      logger_p2p.print_pending();

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

        std::cout << "[recv::RECV] received " << r_recv_msg_packet << " bytes from pid (" << sender_pid << ")" << std::endl;

        recv_buf[r_recv_msg_packet] = '\0'; //end of line to truncate junk
        std::vector<char> recv_packet(recv_buf, recv_buf + r_recv_msg_packet);  // put received data into vector

        for (auto val : recv_packet) printf("%d ", val);
        std::cout <<  "...";

        size_t offset = 0;
        int is_ack;
        // decocde is_ack
        std::memcpy(&(is_ack), recv_packet.data() + offset, sizeof(uint32_t));
        is_ack = ntohl(is_ack);
        offset += sizeof(uint32_t);

        std::cout << "offset: " << offset << ", is_ack: " << is_ack << ", recv_packet_size: " << recv_packet.size() << std::endl;

        // decode messages
        std::vector<Message> msg_recv;
        while (offset < recv_packet.size()) {
          std::cout << "2" << std::endl;
          msg_recv.push_back(DecodeMessage(recv_packet.data(), offset));
        }
        std::cout << "0" << std::endl;
        // upon successful receive trigger event send ack once, ack is "b_pid sn msg 1"
        ack_packet.clear();
        int packet_idx = 0;

        // msg is an ack
        if (is_ack==1){
          for (Message msg: msg_recv){
            int b_pid = msg.b_pid;  // pid of og broadcaster
            std::string ack_msg = msg.msg;
            std::cout << "This is an ack massage: ("<< msg.msg << ", " << msg.b_pid << ") Removing this from msg_pending_for_ack[b_pid][sender_pid]." << std::endl;

            // msg_pending_for_ack is unique; get back ack of msgs sent from all processes
            // if ack_msg is not in pending then pending_for_ack remains unmodified
            logger_p2p.msg_pending_for_ack[b_pid][sender_pid].erase(std::remove(logger_p2p.msg_pending_for_ack[b_pid][sender_pid].begin(), logger_p2p.msg_pending_for_ack[b_pid][sender_pid].end(), ack_msg), logger_p2p.msg_pending_for_ack[b_pid][sender_pid].end());
          }

          //logger_p2p.print_pending();

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
            std::cout << "Encoding ack: (" << msg.msg << ", " << msg.b_pid << ") sent from pid: " << sender_pid<<  " in packet. Num. elements in packet: " << ack_packet.size() << ". This ack packet will be sent to all processes" << std::endl;  

            /*---------*/
            // deliver //
            /*---------*/

            logger_p2p.log_deliver(msg);
  
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
            std::cout << "[recv::SEND] send ack packet to pid (" << host.id << ")" <<  std::endl;
            int64_t r_send_ack_packet = sendto(socket_fd, ack_packet.data(), ack_packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr));  // returns number of characters sent
            if (r_send_ack_packet < 0) {
                std::cout << "[recv::ERROR] sending ack message failed with error: " << strerror(errno) << std::endl;
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
