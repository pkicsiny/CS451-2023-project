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
#define _XOPEN_SOURCE_EXTENDED 1

#include <map>
#include <limits>
#include "zlib.h"
#include <errno.h>
#define WINDOW_SIZE 1

/*-------*/
// begin //
/*-------*/

Logger logger_p2p;

static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  // immediately stop network packet processing
  std::cout << "Immediately stopping network packet processing.\n";

  // write/flush output file if necessary
  std::cout << "Writing output.\n";
  //std::cout << logger_p2p.ss.str() << std::endl;
  logger_p2p.log_delivery();

  // exit directly from signal handler
  exit(0);
}

int log_deliver(const char*, const char, int64_t, const char*);
int log_deliver(const char* output_path, const char msg_type, int64_t pid, const char* msg_buf){

  // log into
  std::ofstream output_file;
  output_file.open(output_path, std::ios_base::app);

  if (output_file.is_open()){
    output_file << msg_type << " " << pid << " " << msg_buf << std::endl;
    output_file.close();
    return 0;
  }else{
    std::cout << "Could not open output file: " << output_path << std::endl;
    return -1;
  }
}

int log_broadcast(const char*, std::string);
int log_broadcast(const char* output_path, std::string msg_buf){
    std::ofstream output_file;
    output_file.open(output_path, std::ios_base::app);
    if (output_file.is_open()){
      output_file << "b " <<  msg_buf << std::endl;
      output_file.close();
      return 0;
    }else{
      std::cout << "Could not open output file: " << output_path << std::endl;
      return -1;
    }
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

  std::cout << "List of resolved hosts is:\n";
  std::cout << "==========================\n";
  auto hosts = parser.hosts();
  for (auto &host : hosts) {
    std::cout << host.id << "\n";
    std::cout << "Human-readable IP: " << host.ipReadable() << "\n";
    std::cout << "Machine-readable IP: " << host.ip << "\n";
    std::cout << "Human-readbale Port: " << host.portReadable() << "\n";
    std::cout << "Machine-readbale Port: " << host.port << "\n";
    std::cout << "\n";
  }
  std::cout << "\n";



  std::cout << "Path to output:\n";
  std::cout << "===============\n";
  std::cout << parser.outputPath() << "\n\n";

  std::cout << "Path to config:\n";
  std::cout << "===============\n";
  std::cout << parser.configPath() << "\n\n";

  std::cout << "Doing some initialization...\n\n";

  /*---------------------*/
  // pid of this process //
  /*---------------------*/

  int my_pid = static_cast<int>(parser.id());

  /*--------------*/
  // Clean output //
  /*--------------*/
 
   if( remove(parser.outputPath()) != 0 ){
     std::cout << parser.outputPath() << " does not exsist." << std::endl;
   }
   else
     std::cout << "Successfully removed " << parser.outputPath() << std::endl; 

  /*-------------*/
  // init logger //
  /*-------------*/

  logger_p2p.output_path = parser.outputPath();
  std::cout << "Initialized logger at: " << logger_p2p.output_path << std::endl;

  /*--------------------*/
  // init port-pid dict //
  /*--------------------*/

  std::map<int, int> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts) {
    port_pid_dict[host.port] = static_cast<int>(host.id);
    std::cout << "port: " << host.port << ": process ID " << port_pid_dict[host.port] << std::endl;
  }

  /*------------------------------------*/
  // init process-received message dict //
  /*------------------------------------*/

  std::map<int64_t, std::vector<std::string>> pid_recv_dict;

  /*------------------*/
  // read config file //
  /*------------------*/

  std::vector<Message> msg_list;

  int NUM_MSG = -1, SERV_PID = -1;
  if (requireConfig){
    std::string l_in;
    std::ifstream config_file;
    std::vector<int> config_file_content_vec;
    config_file.open (parser.configPath());

    if (config_file.is_open()){
      while (getline(config_file, l_in, ' ')){
        std::cout << l_in << std::endl;
        config_file_content_vec.push_back(std::stoi(l_in));
      }
      NUM_MSG = config_file_content_vec[0];  // num messages sent by each process
      SERV_PID = config_file_content_vec[1];  // server process
      config_file.close();
    }else{
      std::cout << "Could not open config file: " << parser.configPath() << std::endl;
    }
    
    if (NUM_MSG == -1 || SERV_PID == -1){
      std::cout << "Reading config failed. NUM_MSG: " << NUM_MSG << ", SERV_PID: " << SERV_PID << std::endl;
      return -1;
    }else{
      std::cout << "Config successfully read. Send " << NUM_MSG << " messages to process " << SERV_PID << std::endl;

      // this bit is adapted to the config but msg_list could be filled up differently
      for (int i=0; i<NUM_MSG; i++){
        msg_list.push_back(Message(i, std::to_string(i+1)));  // sequencing starts from 0
        //msg_list[i] = std::to_string(i+1);
      }
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
      for (int i=0; i<NUM_MSG; i++){
        std::cout << "Message " << i+1 << ": ";
        std::getline(std::cin, msg_cin_buf);

        // msg_list has to be unique, keep while msg_cin_buf is in msg_list
        while (std::find_if(msg_list.begin(), msg_list.end(), [&msg_cin_buf](const Message& m) {return m.msg == msg_cin_buf;}) != msg_list.end()){
  
          // ask again
          std::cout << "Message " << i+1 << ": ";
          std::getline(std::cin, msg_cin_buf);
        }  
        msg_list.push_back(Message(i, msg_cin_buf));  // sequencing starts from 0
      }
    }
  }

  //std::cout << "Message list:" << std::endl;
  //for (auto const& msg_i : msg_list){
  //  std::cout << msg_i.msg << std::endl;
  //}


  /*---------------------*/
  // create pending list //
  /*---------------------*/
 
  std::vector<Message> msg_pending_for_ack;

  /*-------------*/
  // set IP:port //
  /*-------------*/

  // set this process IP and port
  char my_ip[200];
  sprintf(my_ip, "%.10s", hosts[parser.id()-1].ipReadable().c_str());
  int my_port = hosts[parser.id()-1].port;
  std::cout << "my IP:" << my_ip << ", my port:" << my_port << std::endl;

  // set server IP and port
  char serv_ip[200];
  sprintf(serv_ip, "%.10s", hosts[SERV_PID-1].ipReadable().c_str());
  int serv_port = hosts[SERV_PID-1].port;
  std::cout << "server IP:" << serv_ip << ", server port:" << serv_port << std::endl;

  /*-------------------------*/
  // init pid msg count dict //
  /*-------------------------*/

  // keeps track which msg has been sent to which process how many times
  std::map<int, std::map<int, int> > pid_msg_count_dict;
  for (auto &host : hosts) {
    for (size_t i=0; i < msg_list.size(); i++){
    //for (auto const& msg_i : msg_list){
      int sn = msg_list[i].sn;
      pid_msg_count_dict[host.port][sn] = 0;
    }
  }


  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in serv_addr;

  // recv timeout
  struct timeval read_timeout;
  read_timeout.tv_sec = 0;
  read_timeout.tv_usec = 10;

  std::cout << "Broadcasting and delivering messages...\n\n";

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

    char msg_buf[1024];  // buffer for messages
    MsgPacket packet;  // buffer for received message packet
    AckPacket* ack_packet = new AckPacket();  // buffer for ack packet
    std::ostringstream ss_recv;  // stringstream for logging

    /*---------------------------------------------*/
    // wait for messages incoming to server's port //
    /*---------------------------------------------*/

    std::cout << "Waiting to receive messages..." << std::endl;
    while(true){

      // blocking recv
      int64_t msg_recv = recvfrom(socket_fd, msg_buf, sizeof(msg_buf), 0,
          reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_size);  // returns length of incoming message
      if (msg_recv < 0) {
        std::cout << "Receive failed with error" << std::endl;

      /*-------------------------*/
      // process received packet //
      /*-------------------------*/

      }else if (msg_recv != 0) {


        msg_buf[msg_recv] = '\0'; //end of line to truncate junk
        memcpy(&packet, msg_buf, msg_recv);
        int client_port = ntohs(client_addr.sin_port); 
        int client_pid = port_pid_dict[client_port];

        std::cout << "successful recv " << packet.msg_idx << " messages from pid: "<< client_pid << std::endl;

        // upon successful receive trigger event send ack once, ack is "pid sn msg"
        ack_packet->ack_idx = packet.msg_idx;
        for (int i=0; i<packet.msg_idx; i++){
          std::cout << "msg_idx: "<< i<< std::endl;
          std::cout << "got msg: " << packet.m_vec[i].msg  << std::endl;
          /*------------------*/
          // build ack packet //
          /*------------------*/

          Message m_i = packet.m_vec[i];
          AckMessage a_i(client_pid, m_i.sn, m_i.msg);
          ack_packet->a_vec[i] = a_i;



          /*---------*/
          // deliver //
          /*---------*/

          // pid is not in dict i.e. this is the first msg from proc pid
          if (pid_recv_dict.find(client_pid) == pid_recv_dict.end()) {
            pid_recv_dict[client_pid].push_back(m_i.msg);
            logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << m_i.msg << '\n';
  
          // pid is already in dict, if msg is duplicate, do not store in log
          } else {

            // if this is true msg_buf is not yet in dict[pid]
            if (std::find(pid_recv_dict[client_pid].begin(), pid_recv_dict[client_pid].end(), m_i.msg) == pid_recv_dict[client_pid].end()){
              // msg is not yet in dict so log it
              pid_recv_dict[client_pid].push_back(m_i.msg);
              // print ss here before and after this line to see each append is successful or not
              logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << m_i.msg << '\n';
            } // end if
          } // end if
        } // end for packet

        /*-----------------*/
        // send ack packet //
        /*-----------------*/
        size_t ack_packet_size = sizeof(Message)*MAX_PACKET_SIZE + sizeof(int);
        int64_t msg_ack_send = sendto(socket_fd, reinterpret_cast<const char*>(ack_packet), ack_packet_size, 0,
            reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr));  // returns number of characters sent
        if (msg_ack_send < 0) {
            std::cout << "Sending ack message failed with error: " << strerror(errno) << std::endl;
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
      std::cout << "[ERROR] bind(): " << bind_return << std::endl;
      return -1;     
    }

    // set timeout on socket
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);


    socklen_t serv_addr_size  = sizeof(serv_addr);
    std::ostringstream ss_send;
  
    size_t prev_size;
    std::cout << "Start sending messages..." << std::endl;


    while(true){
//    while(!msg_list.empty()){ 
      

      std::cout << "Number of messages in list to send: " << msg_list.size() << std::endl;

      /*------------*/
      // first send //
      /*------------*/

      // with a given window size send message packets
      for (int w=0; w<WINDOW_SIZE; w++){
        
        /*------------------------------------------------------------------*/
        // fill up packet with max 8 messages, or until there are msgs left //
        /*------------------------------------------------------------------*/
        MsgPacket* packet = new MsgPacket();
        packet->msg_idx = 0;
 
        // first send
        std::vector<char> msg_packet;
        int msg_idx = 0;
        if (!msg_list.empty()){
          while((packet->msg_idx < MAX_PACKET_SIZE) && !(msg_list.empty())){
            EncodeMsg(msg_list[0], msg_packet, msg_idx);
            msg_idx++;
            msg_list.erase(msg_list.begin());
        }








        if (!msg_list.empty()){
          while((packet->msg_idx < MAX_PACKET_SIZE) && !(msg_list.empty())){
            Message m_i = msg_list[0];
            packet->m_vec[packet->msg_idx] = m_i;
            packet->msg_idx += 1;
            msg_list.erase(msg_list.begin());
          }
          size_t packet_size = sizeof(Message)*MAX_PACKET_SIZE + sizeof(int);
        
          for (int i=0; i<packet->msg_idx;i++){
            std::cout << "messgae: " << packet->m_vec[i].msg << std::endl;
          }
          /*-----------------------------------------*/
          // send msg packet without sequence number //
          /*-----------------------------------------*/

          int64_t packet_send = sendto(socket_fd, reinterpret_cast<const char*>(packet), packet_size, 0,
              reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
          if (packet_send<0){
            std::cout << "Send failed with error: " << strerror(errno) << std::endl;        
          }

          /*--------------------------------*/
          // log broadcast after first send //
          /*--------------------------------*/
  
          for (auto i=0; i<packet->msg_idx; i++){
            Message m_i = packet->m_vec[i];
            if (pid_msg_count_dict[serv_port][m_i.sn] == 0){
              logger_p2p.ss << 'b' << ' ' << m_i.msg << '\n';
  
              // add msg to pending for ack
              msg_pending_for_ack.push_back(m_i);
            }
            pid_msg_count_dict[serv_port][m_i.sn] += 1;
          }
        } // end first send if
        else if(!(msg_pending_for_ack.empty())){ // resend from pending
          // TODO: ack_packet already  send from pending list, maybe ack before
          // TODO: log delivered periodically    
          MsgPacket packet;
          packet.msg_idx = 0;
          for (Message msg : msg_pending_for_ack){



            packet.m_vec[packet.msg_idx] = msg;
            packet.msg_idx += 1;

            /*-----------------------*/
            // send packet when full //
            /*-----------------------*/

            if (packet.msg_idx == MAX_PACKET_SIZE){
              size_t packet_size = sizeof(Message)*MAX_PACKET_SIZE + sizeof(int);
              int64_t packet_send = sendto(socket_fd, reinterpret_cast<const char*>(&packet), packet_size, 0,
                  reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
              if (packet_send<0){
                std::cout << "Send failed with error: " << strerror(errno) << std::endl;
              }
              packet.msg_idx = 0; // reset packet index to overwrite with new msg
            } // end if send
          } // end for messages

          /*---------------------*/
          // mod MAX_PACKET_SIZE //
          /*---------------------*/

          if (packet.msg_idx>0){
              size_t packet_size = sizeof(Message)*MAX_PACKET_SIZE + sizeof(int);
              int64_t packet_send = sendto(socket_fd, reinterpret_cast<const char*>(&packet), packet_size, 0,
                  reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)); // returns number of characters sent
              if (packet_send<0){
                std::cout << "Send failed with error: " << strerror(errno) << std::endl;
              }            
          } // end if residuals
        } // end if resend unacked
      } // end for one window

      prev_size = msg_pending_for_ack.size();

      std::cout << "msg_list size: " << msg_list.size() << " pending_list size: " << msg_pending_for_ack.size() << std::endl;

      /*----------------------------------------------*/
      // listen to acks, ack is a tuple of (pid, msg) //
      /*----------------------------------------------*/

      std::cout << "Listening to acks..." << std::endl;
      while(true){

        // this is blocking so it listens indefinitely; make it nonblocking by setting a timeout
        AckPacket ack_packet;  // buffer for received message packet
        char ack_buf[1024];
        int64_t ack_recv = recvfrom(socket_fd, ack_buf, sizeof(ack_buf), 0,
            reinterpret_cast<struct sockaddr *>(&serv_addr), &serv_addr_size);  // returns length of incoming message
        if (ack_recv < 0) {
          std::cout << "Exiting ack loo with error code: " << strerror(errno) << std::endl;
          break; // terminate while loop i.e. no more ack to receive
        }else if (ack_recv != 0) {

          /*----------------------*/
          // process acked packet //
          /*----------------------*/

          ack_buf[ack_recv] = '\0'; //end of line to truncate junk
          memcpy(&ack_packet, ack_buf, ack_recv);
          int serv_port = ntohs(serv_addr.sin_port);

          for (auto i=0; i<ack_packet.ack_idx; i++){
            AckMessage a_i = ack_packet.a_vec[i];
            int ack_pid = a_i.pid;
            std::string ack_msg = a_i.msg;
          
            // get index of msg in msg_pending_for_ack
            auto ack_msg_it = std::find_if(msg_pending_for_ack.begin(), msg_pending_for_ack.end(), [&ack_msg](const Message& m) {return m.msg == ack_msg;});

            /*-------------------------------------------*/
            // remove acked msg from msg_pending_for_ack //
            /*-------------------------------------------*/

            if (ack_msg_it != msg_pending_for_ack.end()) {
              auto ack_msg_idx = std::distance(msg_pending_for_ack.begin(), ack_msg_it);
              msg_pending_for_ack.erase(msg_pending_for_ack.begin()+ack_msg_idx);
            }
          }  // end for ack_packet
        }  // end if (ack_recv < 0)
      }  // end while recv

      std::cout << "Num. msg acked in 10 us window: " << prev_size - msg_pending_for_ack.size() << std::endl;
  }  // end while send

  std::cout << "Finished broadcasting." << std::endl;

}  // end if

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    
    //std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
