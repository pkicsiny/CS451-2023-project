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
#include <map>
#include <limits>
#include <errno.h>
#include <unordered_set>

#define _XOPEN_SOURCE_EXTENDED 1
#define MAX_MSG_SN 2147483646  // fixed by assignment, max_int-1 32 bit

#include "utils.hpp"
#include "perfect_link.hpp"
#include "la.hpp"

/*-------*/
// begin //
/*-------*/

Logger logger_p2p;
std::map<int, int> port_pid_map;  // in parser: port: u16 bit, pid: u32 bit (could be u16)

std::vector<Parser::Host> hosts_vec;
unsigned int n_procs = 0;
std::map<int, std::vector<std::string>> proposed_vec;
std::map<int, std::vector<std::string>> accepted_vec;
std::map<int, bool> delivered_map;

static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  std::cout << "Immediately stopping network packet processing.\n";
  std::cout << "Writing output of pid " << logger_p2p.my_pid << " into: " << logger_p2p.output_path << std::endl;
  logger_p2p.log_ld_buffer(1);

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
  }
  std::cout << "There are " << n_procs << " processes in the execution." << std::endl;

  /*-------------*/
  // init logger //
  /*-------------*/

  logger_p2p.output_path = parser.outputPath();
  logger_p2p.my_pid = my_pid;
  std::cout << "Initialized logger at: " << logger_p2p.output_path << " with ld_idx: "<< logger_p2p.ld_idx << "\n\n";
  logger_p2p.ld_buffer = new LogDecision[MAX_LOG_PERIOD];

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
  std::map<int, std::vector<std::string>> proposed_vec;
  config_file.open(parser.configPath());

  // init lattice agreement
  LatticeAgreement la;

  if (config_file.is_open()){
    
    // get first line of config: p, vs, ds
    if(getline(config_file, l_header)){
      std::istringstream iss(l_header);
      while(iss){
      std::string l_header_i;
        iss >> l_header_i;
        if (!l_header_i.empty()) {
            config_file_header.push_back(std::stoi(l_header_i));
        }
      }
    }
    NUM_PROPOSALS = config_file_header[0];
    MAX_LEN_PROPOSAL = config_file_header[1];
    NUM_DISTINCT_ELEMENTS = config_file_header[2];

    la.NUM_PROPOSALS = NUM_PROPOSALS;
  }else{
    std::cout << "[ERROR] Could not open config file: " << parser.configPath() << std::endl;
    return -1;
  }

  config_file.close();

  // read first proposal
  read_single_line(parser.configPath(), 1, proposed_vec);

  if (NUM_PROPOSALS == -1 || MAX_LEN_PROPOSAL == -1 || NUM_DISTINCT_ELEMENTS == -1){
    std::cout << "[ERROR] Reading config failed. NUM_PROPOSALS: " << NUM_PROPOSALS << ", MAX_LEN_PROPOSAL: "<< MAX_LEN_PROPOSAL << ", NUM_DISTINCT_ELEMENTS: " << NUM_DISTINCT_ELEMENTS << std::endl;
    return -1;
  }else{
    std::cout << "Config successfully read. NUM_PROPOSALS: " << NUM_PROPOSALS << ", MAX_LEN_PROPOSAL: "<< MAX_LEN_PROPOSAL << ", NUM_DISTINCT_ELEMENTS: " << NUM_DISTINCT_ELEMENTS << std::endl;

  }


  /*---------------------*/
  // create pending list //
  /*---------------------*/
 
  std::map<int, std::map<int, std::map<int, std::vector<char>>>> resend_map;  // keys are not initted
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
  if (int bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&my_addr), sizeof(my_addr)) < 0){
    std::cout << "[ERROR] bind(): " << strerror(errno) << std::endl;
    return -1;     
  }

  // set timeout on my socket
  setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

  std::cout << "Start sending messages..." << std::endl;
  bool read_new_line = false;

  while(true){
    pl.broadcast(proposed_vec[la.c_idx], logger_p2p, socket_fd, to_addr, la.c_idx, la.apn); // send some messages once
    pl.recv(proposed_vec[la.c_idx], logger_p2p, socket_fd, la.c_idx, la.apn, la.ack_count, la.nack_count);
    pl.resend(logger_p2p, socket_fd, to_addr, la.c_idx, la.apn); // resend all unacked messages once
    la.try_decide(proposed_vec[la.c_idx], pl.do_broadcast, read_new_line, logger_p2p);

    // set to true only upon init_new_consensus
    if (read_new_line){
      read_single_line(parser.configPath(), la.c_idx, proposed_vec);
      read_new_line = false;
    }
  }  // end while send

  std::cout << "Finished broadcasting." << std::endl;

  return 0;
}
