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
//#include "beb.hpp"

/*-------*/
// begin //
/*-------*/

Logger logger_p2p;
std::map<int, int> port_pid_map;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
std::map<int64_t, std::map<int, Message>> pending_msg_map;
std::map<int64_t, std::unordered_set<int>> pending_sn_uset;

std::map<int64_t, std::unordered_set<std::string>> delivered_map;

std::vector<Parser::Host> hosts_vec;
unsigned int n_procs = 0;
std::vector<std::string> proposed_vec;
std::vector<std::string> accepted_vec;

static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  std::cout << "Immediately stopping network packet processing.\n";
  std::cout << "Writing output.\n";
  logger_p2p.log_lm_buffer(1);

/*
  std::cout << "Msgs contained in ack_seen_map at end:" << std::endl;
  for (auto &mes : ack_seen_map){
     std::cout << "(b" << mes.first << ' ';
     for (auto &mes_sn: mes.second){
       std::cout << "sn " << mes_sn.first << "): seen by " << mes_sn.second.size() << " processes: ";
       for (auto &procc: mes_sn.second){
         std::cout << 'p' << procc << ' ';
       }
       std::cout << std::endl;
     }
   }
   std::cout << std::endl;
*/
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

  /*--------------------*/
  // init port-pid dict //
  /*--------------------*/

  hosts_vec = parser.hosts();

  // std::map<int, int> port_pid_map;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts_vec) {
    port_pid_map[host.port] = static_cast<int>(host.id);
    n_procs++;
    //std::cout << "port: " << host.port << ": process ID " << port_pid_map[host.port] << std::endl;
  }
  std::cout << "There are " << n_procs << " processes in the execution." << std::endl;

  /*-------------*/
  // init logger //
  /*-------------*/

  logger_p2p.output_path = parser.outputPath();
  logger_p2p.my_pid = my_pid;
  std::cout << "Initialized logger at: " << logger_p2p.output_path << "\n\n";
  logger_p2p.lm_buffer = new LogMessage[MAX_LOG_PERIOD];
  logger_p2p.lm_idx = 0;

  /*------------------*/
  // read config file //
  /*------------------*/

  // list of sequence numbers that I need to send a given pid
  std::map<int, std::map<int, std::vector<int>>> sn_vec_map;

  int NUM_PROPOSALS = -1;
  int MAX_LEN_PROPOSAL = -1;
  int NUM_DISTINCT_ELEMENTS = -1;
  std::string l_header, l_header_int, l_line, l_line_int;
  std::ifstream config_file;
  std::vector<int> config_file_header;
  std::vector<std::string> proposed_vec;
  config_file.open(parser.configPath());

  if (config_file.is_open()){
    
    // get first line of config: p, vs, ds
    if(getline(config_file, l_header)){
      while (getline(l_header, l_header_int, ' ')){
        std::cout << l_header_int << std::endl;
        config_file_header.push_back(std::stoi(l_header_int));
      }
    }
    NUM_PROPOSALS = config_file_header[0];
    MAX_LEN_PROPOSAL = config_file_header[1];
    NUM_DISTRINCT_ELEMENTS = config_file_header[2];

    // init lattice agreement
    LatticeAgreement la();

    if (getline(config_file, l_line) && la.apn <= NUM_PROPOSALS)
      while (getline(l_line, l_line_int, ' ')){
        std::cout << l_line_int << std::endl;
        proposed_vec.push_back(l_line_int);  // these are strings
      }
    }
    config_file.close();
  }else{
    std::cout << "[ERROR] Could not open config file: " << parser.configPath() << std::endl;
    return -1;
  }
  
  if (NUM_PROPOSALS == -1 || MAX_LEN_PROPOSAL == -1 || NUM_DISTINCT_ELEMENTS == 1){
    std::cout << "[ERROR] Reading config failed. NUM_PROPOSALS: " << NUM_PROPOSALS << ", MAX_LEN_PROPOSAL: "<< MAX_LEN_PROPOSAL << ", NUM_DISTINCT_ELEMENTS: " << NUM_DISTINCT_ELEMENTS << std::endl;
    return -1;
  }else{
    std::cout << "Config successfully read. NUM_PROPOSALS: " << NUM_PROPOSALS << ", MAX_LEN_PROPOSAL: "<< MAX_LEN_PROPOSAL << ", NUM_DISTINCT_ELEMENTS: " << NUM_DISTINCT_ELEMENTS << std::endl;

  }

  /*---------------------*/
  // create pending list //
  /*---------------------*/
 
  std::map<int, std::vector<Message>> resend_map;  // keys are not initted
  logger_p2p.resend_map = resend_map;

  /*----------------*/
  // set my IP:port //
  /*----------------*/

  // set this process IP and port
  char my_ip[200];
  sprintf(my_ip, "%.10s", hosts_vec[parser.id()-1].ipReadable().c_str());
  int my_port = hosts_vec[parser.id()-1].port;
  std::cout << "My socket: " << my_ip << ":" << my_port << ", my process ID: " << my_pid << "\n\n";

  // create beb object, uses inside a pl object
  PerfectLink pl(my_pid, n_procs, hosts_vec);

  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in from_addr, to_addr;  // other process address

  // recv timeout
  struct timeval read_timeout;
  read_timeout.tv_sec = 0;
  read_timeout.tv_usec = 100;

  std::cout << "======================================================" << std::endl;
  std::cout << "Init complete. Broadcasting and delivering messages...\n\n";

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

  std::cout << "Start sending messages..." << std::endl;

  while(true){

    pl.broadcast(proposed_vec, logger_p2p, socket_fd, to_addr, la.c_idx, la.apn); // send some messages once
    pl.recv(logger_p2p, socket_fd, pl.lock_send_vec); // receive messages from other process
    pl.resend(logger_p2p, socket_fd, to_addr, la.c_idx, la.apn); // resend all unacked messages once
    la.try_decide();
      }
    }
  }  // end while send

  std::cout << "Finished broadcasting." << std::endl;

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    
    //std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
