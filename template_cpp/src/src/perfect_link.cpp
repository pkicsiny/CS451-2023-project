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
//#include "beb.hpp"

#include <cmath>

#define MAX_PACKET_SIZE 8  // fixed by assignment

extern std::map<int, int> port_pid_map;
extern std::map<int64_t, std::map<int, Message>> pending_msg_map;
extern std::map<int64_t, std::unordered_set<int>> pending_sn_uset;
extern std::map<int64_t, std::unordered_set<std::string>> delivered_map;

extern std::unordered_set<std::string> pid_send_uset;
extern std::vector<Parser::Host> hosts_vec;
extern unsigned int n_procs;  // urb, num_processes / 2
extern std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_dict;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
extern std::vector<int> next_vec;  // fifo
extern std::vector<int> proposal;
extern size_t len_current_proposal;
extern int num_packets_proposal;

PerfectLink::PerfectLink(int my_pid, int n_procs, std::vector<Parser::Host> hosts_vec){
  this->my_pid = pid;
  this->n_procs = n_procs;
  this->hosts_vec = hosts_vec;
}

// msg_list is the proposal set
void PerfectLink::broadcast(std::vector<std::string> proposed_vec, Logger& logger_p2p, int socket_fd, sockaddr_in to_addr, int c_idx, int apn){

  if (this->do_broadcast){
    for (auto &host : hosts_vec) {

      // config receiver address
      to_addr.sin_family = AF_INET; 
      to_addr.sin_addr.s_addr = inet_addr(host.ipReadable().c_str()); //INADDR_ANY;  
      to_addr.sin_port = htons(host.port);  // port of receiving process
      int to_port = ntohs(to_addr.sin_port); 
      int to_pid = port_pid_map[to_port];

      std::cout << "=========================Send proposal to pid (" << to_pid <<")=========================" << std::endl;

      if (my_pid == to_pid){
        accepted_vec = proposed_vec;
      }else{

        // encodes and returns a msg packet
        std::vector<char> msg_packet = pl.create_send_packet(proposed_vec, c_idx, apn, logger_p2p, to_pid);
 
        // send packet to other pid
        pl.send(msg_packet, to_addr, to_pid, socket_fd);
      }
    } // end for loop on hosts  
  this->do_broadcast = false;
  }  // end if do_broadcast
} // end send()


std::vector<char> PerfectLink::create_send_packet(std::vector<std::string> proposed_vec, int c_idx, int apn, Logger& logger_p2p, int to_pid){

  std::cout << "Encoding proposal" << std::endl;

  // fill up packet with max 8 messages, or until there are msgs left 
  std::vector<char> msg_packet;
  int is_ack = 0;

  // metadata: [is_ack, c_idx, apn, b_pid]
  EncodeMetadata(msg_packet, is_ack, c_idx, apn, my_pid);

  // packet: [int_1, int_2, ..., int_n]
  EncodeProposal(proposed_vec, msg_packet);

  // resend_map is vector of encoded packets
  logger_p2p.resend_map[c_idx][apn][to_pid] = msg_packet;

  return msg_packet;
}


void PerfectLink::send(std::vector<char> msg_packet, sockaddr_in to_addr, int to_pid, int socket_fd){
  if (to_pid != this->my_pid){
    size_t packet_size = msg_packet.size();  // byte size, since sizeof(char)=1
    int64_t r_send_msg_packet = sendto(socket_fd, msg_packet.data(), packet_size, 0,
      reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr)); // returns number of characters sent
    if (r_send_msg_packet<0){
      std::cout << "[PerfectLink::send::ERROR] Send failed with error: " << strerror(errno) << std::endl;        
    }else{
      std::cout << "[PerfectLink::send::SEND_SUCCESSFUL] sent packet of " << r_send_msg_packet << " bytes" << std::endl;
    }
  }
}

// resend_map is a map of encoded packets
void PerfectLink::resend(Logger& logger_p2p, int socket_fd, sockaddr_in to_addr, int c_idx, int apn){

  for (auto &host : hosts_vec) {

    // config receiver address
    to_addr.sin_family = AF_INET; 
    to_addr.sin_addr.s_addr = inet_addr(host.ipReadable().c_str()); //INADDR_ANY;  
    to_addr.sin_port = htons(host.port);  // port of receiving process
    int to_port = ntohs(to_addr.sin_port); 
    int to_pid = port_pid_map[to_port];

    std::cout << "=========================Resending unacked proposal from pid ("<< from_pid <<") to pid ("<< to_pid <<")=========================" << std::endl;

    // loop through all consensuses all apns and send corresponding proposal
    for (c_i=0; c_i<=c_idx; c_i++){
      for (a_i=0; a_i<=apn; a_i++){
        if(!(logger_p2p.resend_map[c_i][a_i][to_pid].empty())){
          pl.send(logger_p2p.resend_map[c_i][a_i][to_pid], to_addr, to_pid, socket_fd);
        } // end if resend unacked
      }
    }
  }  // end for hosts
} // end resend()


void PerfectLink::recv(Logger& logger_p2p, int socket_fd, std::vector<bool>& lock_send_vec){

  // address of sender
  sockaddr_in from_addr;
  socklen_t sizeof_from_addr = sizeof(from_addr);

 // std::cout << "=========================Listening for messages to receive=========================" << std::endl;

  while(true){
      char recv_buf[1024]; // buffer for messages in bytes
      std::vector<char> ack_packet;  // byte array for ack messages

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

        // decode packet
        size_t offset = 0;
        int is_ack_recv, c_idx_recv, apn_recv, b_pid_recv;
        std::vector<std::string> decoded_proposed_vec;
        DecodeMetadata(recv_packet.data(), is_ack_recv, c_idx_recv, b_pid_recv, apn_recv, offset);
        
        // 0: proposal sent, 1: ACK sent back, 2: NACK sent back
        if(is_ack_recv==0){
//          std::cout << "Received packet sent from pid: " << sender_pid<<  " in packet. Total msg packets received: " << total_recv[sender_pid-1] << std::endl;  

          // decode proposed_vec of sender process
          decoded_proposed_vec = DecodeProposal(recv_packet.data(), offset);

          // for comparison first sort the vectors
          std::sort(decoded_proposed_vec.begin(), decoded_proposed_vec.end());
          std::sort(accepted_vec.begin(), accepted_vec.end());

          // compare partner's proposal to my proposal
          if (std::includes(decoded_proposed_vec.begin(), decoded_proposed_vec.end(), accepted_vec.begin(), accepted_vec.end())) {
          
            // send: ACK, proposal_number
            std::vector<char> ack_packet;
            int is_ack = 1;
            EncodeMetadata(ack_packet, is_ack, c_idx_recv, apn_recv, b_pid_recv)
            send_ack(ack_packet, from_addr, socket_fd);

          // accepted_vec is either superset or disjoint to decoded_proposed_vec
          }else{
            // update accepted_vec with new values
            accepted_vec.insert(decoded_proposed_vec.begin(), decoded_proposed_vec.end());

            // send: NACK, proposal_number, accepted_vec
            std::vector<char> nack_packet;
            int is_nack = 2;
            EncodeMetadata(nack_packet, is_ack, c_idx_recv, apn_recv, b_pid_recv)
            EncodeProposal(accepted_vec, nack_packet);
            send_ack(nack_packet, from_addr, socket_fd);
          }
        }else{
          if (my_apn == apn_recv){
            // only 1 proposal for each [c_idx, apn, pid] key; if not in map then map remains unmodified; delete a msg = stop resend
            logger_p2p.resend_map[c_idx][apn_recv].erase(sender_pid);

            if (is_ack_recv==1){ack_count++;}
            else if (is_ack_recv==2){
              nack_count++;
              decoded_proposed_vec = DecodeProposal(recv_packet.data(), offset);
              proposed_vec.insert(decoded_proposed_vec.begin(), decoded_proposed_vec.end());
            }
          }  // end if my_apn==apn_recv
        }  // end if ack_recv==0
      }  // end if recv successful

    } // end while
} // end recv()

void PerfectLink::send_ack(std::vector<char> ack_packet, sockaddr_in to_addr, int socket_fd){
  size_t ack_packet_size = ack_packet.size();  // byte size, since sizeof(char)=1

  int64_t r_send_ack_packet = sendto(socket_fd, ack_packet.data(), ack_packet_size, 0,
      reinterpret_cast<struct sockaddr *>(&to_addr), sizeof(to_addr));  // returns number of characters sent
  if (r_send_ack_packet < 0) {
      //std::cout << "[recv::ERROR] sending ack message failed with error: " << strerror(errno) << std::endl;
  }else{
      std::cout << "[recv::SEND] sent ack packet with bytes: " << r_send_ack_packet << std::endl;

  }
}

