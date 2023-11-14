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

#define MAX_MSG_SN 2147483646  // fixed by assignment, max_int-1 32 bit

#include "utils.hpp"
#include "perfect_link.hpp"

/*-------*/
// begin //
/*-------*/

Logger logger_p2p;
std::map<int, int> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;

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
 // std::map<int, int> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts) {
    port_pid_dict[host.port] = static_cast<int>(host.id);
    //std::cout << "port: " << host.port << ": process ID " << port_pid_dict[host.port] << std::endl;
  }

  /*------------------------------------*/
  // init process-received message dict //
  /*------------------------------------*/

  //std::map<int64_t, std::unordered_set<std::string>> pid_recv_dict;

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

  // create perfect link object
  PerfectLink pl(my_pid);

  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in to_addr;  // other process address

  // recv timeout
  struct timeval read_timeout;
  read_timeout.tv_sec = 0;
  read_timeout.tv_usec = 10;

  std::cout << "======================================================" << std::endl;
  std::cout << "Init complete. Broadcasting and delivering messages...\n\n";

  // on senders (clients): create a socket and bind (assign address) this socket to the receiver
  // on receiver (server): create a socket and listen for incoming events

  /*-----------*/
  // main loop //
  /*-----------*/

  // my socket: AF_INET: IPv4, SOCK_DGRAM: UDP/IP
  if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
      std::cout << "[ERROR] socket()" << socket_fd << std::endl;
      return -1; 
  }
  sockaddr_in my_addr;
  my_addr.sin_family = AF_INET; 
  my_addr.sin_addr.s_addr = INADDR_ANY; //inet_addr(my_ip); //INADDR_ANY;  
  my_addr.sin_port = htons(my_port);  // port of my process
  std::cout << "Client port: " << my_port << " " << my_addr.sin_port << " size: " << sizeof(my_addr) << std::endl;
  if (int bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&my_addr), sizeof(my_addr)) < 0){
    std::cout << "[ERROR] bind(): " << strerror(errno) << std::endl;
    return -1;     
  }

  // set timeout on my socket
  setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

  // S.A.R: send, ack, resend
  std::cout << "Start sending messages..." << std::endl;
  while(true){

    for (auto &host : hosts) {

      // config other address
      to_addr.sin_family = AF_INET; 
      to_addr.sin_addr.s_addr = inet_addr(host.ipReadable().c_str()); //INADDR_ANY;  
      to_addr.sin_port = htons(host.port);  // port of receiving process
      std::cout << "Send to: (machine readable) IP: " << to_addr.sin_addr.s_addr << ", (human readable) port: " << to_addr.sin_port <<  std::endl;

      pl.send(msg_list, logger_p2p, socket_fd, to_addr); // send some messages once
    }

    pl.recv_ack(logger_p2p, socket_fd, to_addr); // wait for acks until queue empty
    pl.resend(logger_p2p, socket_fd, to_addr); // resend all unacked messages once
    pl.recv(logger_p2p, socket_fd, to_addr); // receive messages from other process

    // TODO: make one recv function instead of recv_ack and recv, deliver depending on msg type
    // TODO: make one type of message class, remove ackmessage, denote ack as a boolean
  }  // end while send

  std::cout << "Finished broadcasting." << std::endl;

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    
    //std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
