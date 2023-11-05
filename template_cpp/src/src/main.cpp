#include <chrono>
#include <iostream>
#include <thread>

#include "parser.hpp"
#include "hello.h"
#include <signal.h>

// I load these

#include "utils.hpp"

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
#define WINDOW_SIZE 50
#define MAX_LOG_PERIOD 100
#define MAX_MSG_SN 2147483646  // fixed by assignment, max_int-1 32 bit

/*-------*/
// begin //
/*-------*/

Logger logger_p2p;


static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  std::cout << "Immediately stopping network packet processing.\n";
  std::cout << "Writing output.\n";
  logger_p2p.log_lm_buffer();
  std::cout << "[PENDING] " << logger_p2p.msg_pending_for_ack.size() << std::endl;

  // exit directly from signal handler
  exit(0);
}


int main(int argc, char **argv) {
  signal(SIGTERM, stop);
  signal(SIGINT, stop);

  // `true` means that a config file is required.
  // Call with `false` if no config file is necessary.
  bool requireConfig = true;
  Parser parser(argc, argv);
  parser.parse();

  hello();
  std::cout << std::endl;
  std::cout << "My PID: " << getpid() << "\n";
  std::cout << "From a new terminal type `kill -SIGINT " << getpid() << "` or `kill -SIGTERM "
            << getpid() << "` to stop processing packets\n\n";
  std::cout << "My ID: " << parser.id() << "\n\n";
  std::cout << "Path to output: " << parser.outputPath() << "\n\n";
  std::cout << "Path to config: " << parser.configPath() << "\n\n";
  std::cout << "Doing some initialization...\n\n";

  /*---------------------*/
  // pid of this process //
  /*---------------------*/

  int my_pid = static_cast<int>(parser.id());

  /*--------------*/
  // Clean output //
  /*--------------*/
 
   if( remove(parser.outputPath()) != 0 ){
     std::cout << parser.outputPath() << " does not exsist." << "\n\n";
   }
   else
     std::cout << "Successfully removed " << parser.outputPath() << "\n\n";

  /*-------------*/
  // init logger //
  /*-------------*/

  logger_p2p.output_path = parser.outputPath();
  std::cout << "Initialized logger at: " << logger_p2p.output_path << "\n\n";
  logger_p2p.lm_buffer = new LogMessage[MAX_LOG_PERIOD];
  logger_p2p.lm_idx = 0;

  /*--------------------*/
  // init port-pid dict //
  /*--------------------*/

  auto hosts = parser.hosts();
  std::map<int, int> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts) {
    port_pid_dict[host.port] = static_cast<int>(host.id);
    //std::cout << "port: " << host.port << ": process ID " << port_pid_dict[host.port] << std::endl;
  }

  /*------------------------------------*/
  // init process-received message dict //
  /*------------------------------------*/

  std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;

  /*------------------*/
  // read config file //
  /*------------------*/

  MessageList msg_list;
  msg_list.sn_idx = 0;

  int NUM_MSG = -1, SERV_PID = -1;
  if (requireConfig){
    std::string l_in;
    std::ifstream config_file;
    std::vector<int> config_file_content_vec;
    config_file.open (parser.configPath());

    if (config_file.is_open()){
      while (getline(config_file, l_in, ' ')){
        //std::cout << l_in << std::endl;
        config_file_content_vec.push_back(std::stoi(l_in));
      }
      NUM_MSG = config_file_content_vec[0];  // num messages sent by each process
      SERV_PID = config_file_content_vec[1];  // server process
      assert(NUM_MSG<=MAX_MSG_SN+1);
      config_file.close();
    }else{
      std::cout << "[ERROR] Could not open config file: " << parser.configPath() << std::endl;
      return -1;
    }
    
    if (NUM_MSG == -1 || SERV_PID == -1){
      std::cout << "[ERROR] Reading config failed. NUM_MSG: " << NUM_MSG << ", SERV_PID: " << SERV_PID << std::endl;
      return -1;
    }else{
      std::cout << "Config successfully read. Send " << NUM_MSG << " messages to process ID " << SERV_PID << "\n\n";
      msg_list.msg_remaining = NUM_MSG;
      if (my_pid != SERV_PID){msg_list.refill();}
    }
  }else{  // if no config, read from inout stream or something
    SERV_PID = 1;

    // I need the messages only if I am a sender (client)
    if (my_pid != SERV_PID){

      std::cout << "Enter the number of messages: ";
      std::cin >> NUM_MSG;
  
      // check for correctness of input (this handles overflow)
      while (!std::cin.good())
      {
      
        // reset and ignore rest of cin (e.g. upon overflown input the cin is capped at max int)
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // ask again
        std::cout << "Enter the number of messages: ";
        std::cin >> NUM_MSG;
      }
      // reset and ignore rest of cin
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

      // at this point NUM_MSG is a valid int, but it has to be >0
      if (NUM_MSG<=0){
        std::cout << "NUM_MSG is invalid: " << NUM_MSG << std::endl;
        return -1;
      }

      // cin the UNIQUE messages and push back to msg_list; one msg is the full line
      std::string msg_cin_buf;
      msg_list.msg_remaining = NUM_MSG;

      for (int i=0; i<NUM_MSG; i++){
        std::cout << "Message " << i+1 << ": ";
        std::getline(std::cin, msg_cin_buf);

        // msg_list has to be unique, keep while msg_cin_buf is in msg_list
        while (std::find_if(msg_list.msg_list.begin(), msg_list.msg_list.end(), [&msg_cin_buf](const Message& m) {return m.msg == msg_cin_buf;}) != msg_list.msg_list.end()){
  
          // ask again
          std::cout << "Message " << i+1 << ": ";
          std::getline(std::cin, msg_cin_buf);
        }  

        msg_list.msg_list.push_back(Message(msg_list.sn_idx, msg_cin_buf));  // sequencing starts from 0
        msg_list.sn_idx++;
        msg_list.msg_remaining--;
      }
    }
  }

  /*---------------------*/
  // create pending list //
  /*---------------------*/
 
  std::vector<Message> msg_pending_for_ack;
  logger_p2p.msg_pending_for_ack = msg_pending_for_ack;

  /*-------------*/
  // set IP:port //
  /*-------------*/

  // set this process IP and port
  char my_ip[200];
  sprintf(my_ip, "%.10s", hosts[parser.id()-1].ipReadable().c_str());
  int my_port = hosts[parser.id()-1].port;
  std::cout << "My socket: " << my_ip << ":" << my_port << ", my process ID: " << my_pid << "\n\n";

  // set server IP and port
  char serv_ip[200];
  sprintf(serv_ip, "%.10s", hosts[SERV_PID-1].ipReadable().c_str());
  int serv_port = hosts[SERV_PID-1].port;
  std::cout << "Server socket: " << serv_ip << ":" << serv_port << ", server process ID: " << SERV_PID << "\n\n";

  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in serv_addr;

  // recv timeout
  struct timeval read_timeout;
  read_timeout.tv_sec = 0;
  read_timeout.tv_usec = 10;

  std::cout << "======================================================" << std::endl;
  std::cout << "Init complete. Broadcasting and delivering messages...\n\n";

  // on senders (clients): create a socket and bind (assign address) this socket to the receiver
  // on receiver (server): create a socket and listen for incoming events

  /*--------*/
  // server //
  /*--------*/

  if (my_pid == SERV_PID){
    std::cout << "---I am the server---" << std::endl;

    // AF_INET: IPv4, SOCK_DGRAM: UDP/IP
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
        std::cout << "[ERROR] socket()" << socket_fd << std::endl;
        return -1; 
    }

    // init with 0s
    std::memset(reinterpret_cast<struct sockaddr *>(&serv_addr), 0, sizeof(serv_addr));

    /* setup the host_addr structure for use in bind call */

    // server byte order
    serv_addr.sin_family = AF_INET; 
                                                                                                                     
    // automatically be filled with current host's IP address
    serv_addr.sin_addr.s_addr = INADDR_ANY; //inet_addr(serv_ip); //INADDR_ANY;  
                                                                                                                     
    // convert short integer value for port must be converted into network byte order
    serv_addr.sin_port = htons(serv_port);  // port of server process
    std::cout << "server port: " << htons(serv_port) << " " << serv_addr.sin_port <<  std::endl;
                                                                                                                     
    // bind socket to server address
    if (int bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0){
      std::cout << "[ERROR] bind(): " << bind_return << std::endl;
      return -1;     
    }
    
    // client address
    sockaddr_in client_addr;
    socklen_t client_addr_size  = sizeof(client_addr);

    char recv_buf[1024]; // buffer for messages in bytes
    std::vector<char> ack_packet;  // byte array for ack messages

    /*---------------------------------------------*/
    // wait for messages incoming to server's port //
    /*---------------------------------------------*/

    std::cout << "Waiting to receive messages..." << std::endl;
    while(true){

      // blocking recv
      int64_t r_recv_msg_packet = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0,
          reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_size);  // returns length of incoming message
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

        int client_port = ntohs(client_addr.sin_port); 
        int client_pid = port_pid_dict[client_port];

        // upon successful receive trigger event send ack once, ack is "pid sn msg"
        ack_packet.clear();
        for (Message msg: msg_recv){

          /*------------------*/
          // build ack packet //
          /*------------------*/

          Ack ack(client_pid, msg.sn, msg.msg);
          EncodeAck(ack, ack_packet, client_pid);
          //std::cout << "Encoding ack: " << msg.msg << " pid: " << client_pid<<  " in packet. Num. elements in packet: " << ack_packet.size() << std::endl;

          /*---------*/
          // deliver //
          /*---------*/

          // pid is not in dict i.e. this is the first msg from proc pid
          if (pid_recv_dict.find(client_pid) == pid_recv_dict.end()) {
            pid_recv_dict[client_pid].insert(msg.msg);
            //logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg.msg << '\n';
            LogMessage lm;
            lm.m = msg;
            lm.sender_pid = client_pid;
            lm.msg_type = 'd';
            logger_p2p.lm_buffer[logger_p2p.lm_idx] = lm;
            logger_p2p.lm_idx++;
            if(logger_p2p.lm_idx == MAX_LOG_PERIOD){
              logger_p2p.log_lm_buffer();
            }                                              

          // pid is already in dict, if msg is duplicate, do not store in log
          } else {

            // if this is true msg_buf is not yet in dict[pid]
            //if (std::find(pid_recv_dict[client_pid].begin(), pid_recv_dict[client_pid].end(), msg.msg) == pid_recv_dict[client_pid].end()){
            if (pid_recv_dict[client_pid].find(msg.msg) == pid_recv_dict[client_pid].end()){
              // msg is not yet in dict so log it
              pid_recv_dict[client_pid].insert(msg.msg);
              //logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg.msg << '\n';
              LogMessage lm;
              lm.m = msg;
              lm.sender_pid = client_pid;
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
            reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr));  // returns number of characters sent
        if (r_send_ack_packet < 0) {
            std::cout << "Sending ack message failed with error: " << strerror(errno) << std::endl;
        }else{
          //std::cout << "[SEND] sent ack packet with bytes: " << r_send_ack_packet << std::endl;
          //for (auto val : ack_packet) printf("%d ", val);
          //std::cout << "..." << std::endl;
        }
      } // if (msg_recv < 0)
    } // while recv
 
  /*--------*/
  // client //
  /*--------*/

  }else{
    std::cout << "---I am a client---" << std::endl;

    // AF_INET: IPv4, SOCK_DGRAM: UDP/IP
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
        std::cout << "[ERROR] socket()" << socket_fd << std::endl;
        return -1; 
    }

    std::cout << "Configuring server address..." << std::endl;
    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_addr.s_addr = inet_addr(serv_ip); //INADDR_ANY;  
    serv_addr.sin_port = htons(serv_port);  // port of server procesr
    std::cout << "Server address successfully configured. (machine readable) IP: " << serv_addr.sin_addr.s_addr << ", (human readable) port: " << serv_addr.sin_port <<  std::endl;

    // bind client address, otherwise process uses a random port to sent msg
    sockaddr_in client_addr;
    client_addr.sin_family = AF_INET; 
    client_addr.sin_addr.s_addr = INADDR_ANY; //inet_addr(my_ip); //INADDR_ANY;  
    client_addr.sin_port = htons(my_port);  // port of my process
    std::cout << "Client port: " << my_port << " " << client_addr.sin_port << " size: " << sizeof(client_addr) << std::endl;
    if (int bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr)) < 0){
      std::cout << "[ERROR] bind(): " << strerror(errno) << std::endl;
      return -1;     
    }

    // set timeout on socket
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);


    socklen_t serv_addr_size  = sizeof(serv_addr);
    std::ostringstream ss_send;
  
    size_t prev_size;
    char ack_buf[1024];
    std::cout << "Start sending messages..." << std::endl;

    // S.A.R: send, ack, resend
    while(true){

      /*------------*/
      // first send //
      /*------------*/
      
      // wait before resend
      //if (msg_list.msg_list.empty()){
      //  usleep(1000);
      //}
 
      int w = 0;
      while((!msg_list.msg_list.empty()) & (w<WINDOW_SIZE)){  
        //std::cout << "Number of messages in list to send: " << msg_list.msg_list.size() << " =========================" << std::endl;

        // fill up packet with max 8 messages, or until there are msgs left 
        std::vector<char> msg_packet;
        int msg_idx = 0;
        while((msg_idx < MAX_PACKET_SIZE) && !(msg_list.msg_list.empty())){
          EncodeMessage(msg_list.msg_list[0], msg_packet, msg_idx);
          //std::cout << "Encoding message: " << msg_list.msg_list[0].msg << " in packet at index " << msg_idx << ". Num. elements in packet: " << msg_packet.size() << std::endl;
          msg_idx += 1;
          //std::cout << "After encoding message: " << msg_packet.size() << " msg_idx: " << msg_idx  << std::endl;

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
            reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
        if (r_send_msg_packet<0){
          std::cout << "Send failed with error: " << strerror(errno) << std::endl;        
        }else{
          //std::cout << "[SEND] sent packet with bytes: " << r_send_msg_packet << std::endl;
          //for (auto val : msg_packet) printf("%d ", val);
          //std::cout << "..." << std::endl;
        }
        w++;
      } // end while send window

      /*----------------------------------------------*/
      // listen to acks, ack is a tuple of (pid, msg) //
      /*----------------------------------------------*/
      
      while(!(logger_p2p.msg_pending_for_ack.empty())){
        //std::cout << "Listening to acks...\n==========================" << std::endl;

        prev_size = logger_p2p.msg_pending_for_ack.size();

        // this is blocking so it listens indefinitely; make it nonblocking by setting a timeout

        int64_t r_recv_ack_packet = recvfrom(socket_fd, ack_buf, sizeof(ack_buf), 0,
            reinterpret_cast<struct sockaddr *>(&serv_addr), &serv_addr_size);  // returns length of incoming message
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

          int serv_port = ntohs(serv_addr.sin_port);

          for (Ack ack : ack_vec){
            int ack_pid = ack.pid;
            std::string ack_msg = ack.msg;

            // remove acked msg from msg_pending_for_ack //
            //std::cout << "Removing ack message: " << ack_msg << " from pid: "<< ack_pid << " from pending." << std::endl;    

            // msg_pending_for_ack is unique; only get back ack of msgs that were sent from this process
            // if ack_msg is not in pending then pending remains unmodified
            logger_p2p.msg_pending_for_ack.erase(std::remove(logger_p2p.msg_pending_for_ack.begin(), logger_p2p.msg_pending_for_ack.end(), ack_msg), logger_p2p.msg_pending_for_ack.end());

            //auto ack_msg_it = std::find_if(logger_p2p.msg_pending_for_ack.begin(), logger_p2p.msg_pending_for_ack.end(), [&ack_msg](const Message& m) {return m.msg == ack_msg;});
            //if (ack_msg_it != logger_p2p.msg_pending_for_ack.end()) {
            //  auto ack_msg_idx = std::distance(logger_p2p.msg_pending_for_ack.begin(), ack_msg_it);
            //  logger_p2p.msg_pending_for_ack.erase(logger_p2p.msg_pending_for_ack.begin()+ack_msg_idx);
            //}
          }  // end for ack_packet
        }  // end if (ack_recv < 0)

        //std::cout << "[STATUS] msg_list size: " << msg_list.msg_list.size() << " pending_list size: " << logger_p2p.msg_pending_for_ack.size() << " Num. msg acked in 10 us window: " << prev_size - logger_p2p.msg_pending_for_ack.size() << std::endl;
      }  // end while recv_ack
      

      /*-------------------------*/
      // resend unacked messages //
      /*-------------------------*/
      
      if(!(logger_p2p.msg_pending_for_ack.empty())){
        //std::cout << "Resending unacked messages\n======================= " << std::endl;

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
                reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
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
                reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
            if (r_resend_msg_packet<0){
              std::cout << "Send failed with error: " << strerror(errno) << std::endl;
            }else{
              //std::cout << "[SEND] resent packet with bytes: " << r_resend_msg_packet << std::endl;
              //for (auto val : resend_packet) printf("%d ", val);
              //std::cout << "..." << std::endl;
            } 
        } // end if residuals
      } // end if resend unacked
      

    }  // end while send

  std::cout << "Finished broadcasting." << std::endl;

  } // end if server/client

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    
    //std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
