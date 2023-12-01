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

extern std::map<int, int> port_pid_map;
//extern std::map<int64_t, std::unordered_set<Message>> recv_pending_map;
//extern std::map<int64_t, std::unordered_set<Message>> delivered_map;
extern std::map<int64_t, std::vector<Message>> recv_pending_map;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;

extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_dict;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern std::vector<int> next_vec;  // fifo

PerfectLink::PerfectLink(int pid){
  my_pid = pid;
}

void PerfectLink::send(MessageList& msg_list, Logger& logger_p2p, int socket_fd, sockaddr_in to_addr){

  int to_port = ntohs(to_addr.sin_port); 
  int to_pid = port_pid_map[to_port];

  int w = 0;

  if (!lock_send_vec[to_pid-1]){
//    if(1){
    while((!msg_list.msg_list.empty()) && (w<WINDOW_SIZE)){  
//      std::cout << "=========================Number of messages in list to send to pid (" << to_pid <<"): " << msg_list.msg_list.size() << "=========================" << std::endl;

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
//        std::cout << "Encoding message: " << msg_list.msg_list[0].msg << " in packet at index " << msg_idx << ". Num. elements in packet: " << msg_packet.size() << std::endl;

        // log boradcast event of single message
        logger_p2p.log_broadcast(msg_list.msg_list[0], 0);
  
        // wait for ack of msg og boradcast from my_pid to to_pid
        logger_p2p.relay_map[my_pid][to_pid].push_back(msg_list.msg_list[0]);
  //      logger_p2p.print_pending();
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
        //std::cout << "[send::ERROR] Send failed with error: " << strerror(errno) << std::endl;        
      }else{
        //std::cout << "[send::SEND] sent packet with bytes: " << r_send_msg_packet << std::endl;
      }
      w++;
    } // end while send window
    // sent WINDOW_SIZE*MAX_PACKET_SIZE messages. Lock send and unlock only after I acked all sent msgs
    lock_send_vec[to_pid-1] = true;
  } // end if !lock_send_vec

} // end send()


void PerfectLink::resend(Logger& logger_p2p, int socket_fd, sockaddr_in to_addr, int from_pid, int to_pid){

      //std::cout << "=========================Resending unacked messages og broadcast from pid ("<< from_pid <<") to pid ("<< to_pid <<")=========================" << std::endl;
      //logger_p2p.print_pending();

      // new keys initted here with empty vector value
      if(!(logger_p2p.relay_map[from_pid][to_pid].empty())){

        std::vector<char> resend_packet;
        int msg_idx = 0;

        for (Message msg : logger_p2p.relay_map[from_pid][to_pid]){

          // encode is_ack at beginning of packet
          if (msg_idx==0){
            uint32_t is_ack_ser = htonl(0);
            resend_packet.insert(resend_packet.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes
          }

          EncodeMessage(msg, resend_packet, msg_idx);
          msg_idx += 1;
          //std::cout << "Encoding message: (" << msg.msg << ", " << msg.b_pid << ")." << std::endl;  


          // send packet when full //
          if (msg_idx == MAX_PACKET_SIZE){
            size_t packet_size = resend_packet.size();  // byte size, since sizeof(char)=1
            //std::cout << "Resending packet with size: "<< packet_size << std::endl;
            int64_t r_resend_msg_packet = sendto(socket_fd, resend_packet.data(), packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
//              std::cout << "[resend::SEND] resend to pid ("<< to_pid <<") failed with error: " << strerror(errno) << std::endl;
            }else{
              total_resent[to_pid-1]++;
 //             std::cout << "[resend::SEND] resent packet to pid ("<< to_pid <<") with bytes: " << r_resend_msg_packet << ". Total packets resent: " << total_resent[to_pid-1] << std::endl;
              //for (auto val : resend_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            } 
            msg_idx = 0; // reset packet index to overwrite with new msg
            resend_packet.clear();
          } // end if send

        } // end for messages

        // mod MAX_PACKET_SIZE //
        if ((msg_idx>0) && (msg_idx<MAX_PACKET_SIZE)){  // this is a packet smaller than MAX_PACKET_SIZE messages
            size_t packet_size = resend_packet.size();  // byte size, since sizeof(char)=1
            int64_t r_resend_msg_packet = sendto(socket_fd, resend_packet.data(), packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
              //std::cout << "[resend::SEND] resend to pid ("<< to_pid <<") failed with error: " << strerror(errno) << std::endl;
            }else{
             // std::cout << "[resend::SEND] resent packet to pid ("<< to_pid <<") with bytes: " << r_resend_msg_packet << std::endl;
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

//  std::cout << "=========================Listening for messages to receive=========================" << std::endl;

  while(true){
      char recv_buf[1024]; // buffer for messages in bytes
      std::vector<char> ack_packet;  // byte array for ack messages

      //logger_p2p.print_pending();

      // blocking recv
      int64_t r_recv_msg_packet = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0,
          reinterpret_cast<struct sockaddr *>(&from_addr), &sizeof_from_addr);  // returns length of incoming message

      if (r_recv_msg_packet < 0) {
//        std::cout << "[recv::TIMEOUT] recvfrom timed out or no more incoming data: " << strerror(errno) << std::endl;
        break;
      // decode single msg received
      }else if (r_recv_msg_packet != 0) {
        int sender_port = ntohs(from_addr.sin_port); 
        int sender_pid = port_pid_map[sender_port];

        //std::cout << "[recv::RECV] received " << r_recv_msg_packet << " bytes from pid (" << sender_pid << ")" << std::endl;

        recv_buf[r_recv_msg_packet] = '\0'; //end of line to truncate junk
        std::vector<char> recv_packet(recv_buf, recv_buf + r_recv_msg_packet);  // put received data into vector

        //for (auto val : recv_packet) printf("%d ", val);
        //std::cout <<  "...";

        size_t offset = 0;
        int is_ack;
        // decocde is_ack
        std::memcpy(&(is_ack), recv_packet.data() + offset, sizeof(uint32_t));
        is_ack = ntohl(is_ack);
        offset += sizeof(uint32_t);

        //std::cout << "offset: " << offset << ", is_ack: " << is_ack << ", recv_packet_size: " << recv_packet.size() << std::endl;

        // decode messages
        std::vector<Message> msg_recv;
        while (offset < recv_packet.size()) {
          msg_recv.push_back(DecodeMessage(recv_packet.data(), offset));
        }
        // upon successful receive trigger event send ack once, ack is "b_pid sn msg 1"
        ack_packet.clear();
        int packet_idx = 0;

        // msg is an ack
        if (is_ack==1){
          total_ack_recv[sender_pid-1]++;
  //        std::cout << "Received ack packet from sender pid: "<<sender_pid << ". Total ack packets received: " << total_ack_recv[sender_pid-1] << std::endl;

          for (Message msg: msg_recv){
            int b_pid = msg.b_pid;  // pid of og broadcaster
            std::string ack_msg = msg.msg;
 //           std::cout << "Received ack message: (b"<< msg.b_pid << " " << msg.sn << ") from sender pid: "<<sender_pid << '.'<< " Total ack packets received: " << total_ack_recv[sender_pid-1] << std::endl;

            // relay_map is unique; get back ack of msgs sent from all processes
            // if ack_msg is not in pending then pending_for_ack remains unmodified
            // delete a msg = stop relaying (b_pid msg) to (sender_pid)
            logger_p2p.relay_map[b_pid][sender_pid].erase(std::remove(logger_p2p.relay_map[b_pid][sender_pid].begin(), logger_p2p.relay_map[b_pid][sender_pid].end(), ack_msg), logger_p2p.relay_map[b_pid][sender_pid].end());

            // if sender_pid has acked all msgs b by me, unlock sending next WINDOW_SIZE*MAX_PACKET_SIZE msgs
            if ((b_pid==my_pid) && (logger_p2p.relay_map[my_pid][sender_pid].empty()) && (lock_send_vec[sender_pid-1]=true)){lock_send_vec[sender_pid-1]=false;}
          }

          //logger_p2p.print_pending();

        // this msg is not an ack i.e. it has been broadcast or relayed. It comes from msg.b_pid.
        }else{
          total_recv[sender_pid-1]++;
//          std::cout << "Received packet sent from pid: " << sender_pid<<  " in packet. Total msg packets received: " << total_recv[sender_pid-1] << std::endl;  
          int b_pid_from_msg=0;
          for (Message msg: msg_recv){

            /*------------------*/
            // build ack packet //
            /*------------------*/
            b_pid_from_msg = msg.b_pid;
            if (packet_idx==0){
              uint32_t is_ack_ser = htonl(1);
              ack_packet.insert(ack_packet.end(), reinterpret_cast<char*>(&is_ack_ser), reinterpret_cast<char*>(&is_ack_ser) + sizeof(uint32_t));  // 4 bytes
            }
            Message ack(msg.b_pid, msg.sn, msg.msg, 1);
            EncodeMessage(ack, ack_packet, packet_idx);
            packet_idx++;
//            std::cout << "Received-message: (b" << msg.b_pid << " " << msg.sn << ") sent from pid: " << sender_pid<<  " in packet." << " Total msg packets received: " << total_recv[sender_pid-1] << std::endl;  

            /*---------*/
            // deliver //
            /*---------*/

            // this msg was seen by sender_pid since it sent/relayed it to me
            logger_p2p.add_to_ack_seen(msg, sender_pid, is_ack);
            if (my_pid != sender_pid){ // if got msg from someone else I saw it so add to ack
              logger_p2p.add_to_ack_seen(msg, my_pid, is_ack);
            }
            logger_p2p.log_deliver(msg, is_ack, 0);  // also adds relayable msg to pending
          } // end for packet
 
          /*-------------------------------*/
          // send ack packet to sender_pid //
          /*-------------------------------*/


            size_t ack_packet_size = ack_packet.size();  // byte size, since sizeof(char)=1
            struct sockaddr_in to_addr;  // other process address
  
              // if i got msg from myself: send ack to myself to stop resending to myself
              // I dont send ack to myself if (ack = pls stop flooding me with this msg since i got it):
              // 1 - I got back my msg relayed from someone else (if im correct ill receive all msgs from me through send/resend)
              // 2 - I got a msg from someone else (i dont expect an ack from myself since im not relaying to myself)

              // msgs sent by myself I dont send ack but wait for others relay;
              // 3 - got msg b myself: ack to sender to stop resend / relay

            //std::cout << "[recv::SEND] send ack packet to pid (" << host.id << ")" <<  std::endl;
            int64_t r_send_ack_packet = sendto(socket_fd, ack_packet.data(), ack_packet_size, 0,
                reinterpret_cast<struct sockaddr *>(&from_addr), sizeof(from_addr));  // returns number of characters sent
            if (r_send_ack_packet < 0) {
                //std::cout << "[recv::ERROR] sending ack message failed with error: " << strerror(errno) << std::endl;
            }else{
              total_ack_sent[sender_pid-1]++;
//              std::cout << "[recv::SEND] sent ack packet with bytes: " << r_send_ack_packet << ". Total ack packets sent: " << total_ack_sent[sender_pid-1] << std::endl;

              //for (auto val : ack_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            }
        }  // end if ack==1
      }  // end if recv successful

    } // end while
} // end recv()
