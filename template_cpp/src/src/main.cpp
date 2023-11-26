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
std::unordered_set<std::string> pid_send_dict;
std::vector<Parser::Host> hosts;
std::map<int, std::map<int, std::unordered_set<int>>> ack_seen_dict;  // urb, ack[msg.b_pid][msg.sn]=[sender_ids]
unsigned int n_procs = 0;

static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  std::cout << "Immediately stopping network packet processing.\n";
  std::cout << "Writing output.\n";
  logger_p2p.log_lm_buffer();
  logger_p2p.print_pending();

  std::cout << "Msgs contained in ack_seen_dict at end:" << std::endl;
  for (auto &mes : ack_seen_dict){
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

  hosts = parser.hosts();

  // std::map<int, int> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts) {
    port_pid_dict[host.port] = static_cast<int>(host.id);
    n_procs++;
    //std::cout << "port: " << host.port << ": process ID " << port_pid_dict[host.port] << std::endl;
  }
  std::cout << "there are " << n_procs << " processes in the execution." << std::endl;

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

  std::map<int, MessageList> msg_list_vec;
  unsigned int p = 1;
  while (p<=n_procs){
    msg_list_vec[p].sn_idx = 0;
    p++;
  }

  int NUM_MSG = -1;
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
      assert(NUM_MSG<=MAX_MSG_SN+1);
      config_file.close();
    }else{
      std::cout << "[ERROR] Could not open config file: " << parser.configPath() << std::endl;
      return -1;
    }
    
    if (NUM_MSG == -1){
      std::cout << "[ERROR] Reading config failed. NUM_MSG: " << NUM_MSG << std::endl;
      return -1;
    }else{
      std::cout << "Config successfully read. Send " << NUM_MSG << " messages to all (" << n_procs << ") processes\n\n";

      p = 1;
      while(p<=n_procs){
        msg_list_vec[p].msg_remaining = NUM_MSG;
        msg_list_vec[p].refill(my_pid, 0);
        p++;
      }
    }
  }else{  // if no config, read from inout stream or something

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

    p = 1;
    while(p<=n_procs){
      msg_list_vec[p].msg_remaining = NUM_MSG;
      p++;
    }

    MessageList msg_list_user;
    for (int i=0; i<NUM_MSG; i++){
      std::cout << "Message " << i+1 << ": ";
      std::getline(std::cin, msg_cin_buf);

      // msg_list has to be unique, keep while msg_cin_buf is in msg_list
      while (std::find_if(msg_list_user.msg_list.begin(), msg_list_user.msg_list.end(), [&msg_cin_buf](const Message& m) {return m.msg == msg_cin_buf;}) != msg_list_user.msg_list.end()){
  
        // ask again
        std::cout << "Message " << i+1 << ": ";
        std::getline(std::cin, msg_cin_buf);
      }  

      msg_list_user.msg_list.push_back(Message(my_pid, msg_list_user.sn_idx, msg_cin_buf, 0));  // sequencing starts from 0
      msg_list_user.sn_idx++;
      msg_list_user.msg_remaining--;
    }
    p = 1;
    while (p<=n_procs){
      msg_list_vec[p] = msg_list_user;
      p++;
    }
  }

  /*---------------------*/
  // create pending list //
  /*---------------------*/
 
  std::map<int, std::map<int, std::vector<Message>>> msg_pending_for_ack;  // keys are not initted
  logger_p2p.msg_pending_for_ack = msg_pending_for_ack;

  /*----------------*/
  // set my IP:port //
  /*----------------*/

  // set this process IP and port
  char my_ip[200];
  sprintf(my_ip, "%.10s", hosts[parser.id()-1].ipReadable().c_str());
  int my_port = hosts[parser.id()-1].port;
  std::cout << "My socket: " << my_ip << ":" << my_port << ", my process ID: " << my_pid << "\n\n";

  // create perfect link object
  PerfectLink pl(my_pid);

  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in from_addr, to_addr;  // other process address

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

  usleep(5000000);

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
      pl.send(msg_list_vec[int(host.id)], logger_p2p, socket_fd, to_addr); // send some messages once
    }
    pl.recv(logger_p2p, socket_fd); // receive messages from other process
    for (auto &from_host : hosts) {
      from_addr.sin_family = AF_INET; 
      from_addr.sin_addr.s_addr = inet_addr(from_host.ipReadable().c_str()); //INADDR_ANY;  
      from_addr.sin_port = htons(from_host.port);  // port of receiving process
      int from_pid = port_pid_dict[from_host.port];
      for (auto &to_host : hosts){
        to_addr.sin_family = AF_INET; 
        to_addr.sin_addr.s_addr = inet_addr(to_host.ipReadable().c_str()); //INADDR_ANY;  
        to_addr.sin_port = htons(to_host.port);  // port of receiving process
        int to_pid = port_pid_dict[to_host.port];

        // i resend but not relay msgs broadcasted from myself to myself: pending[my_pid][my_pid] fills up at first send
        // in this implementation relay = resend
        pl.resend(logger_p2p, socket_fd, to_addr, from_pid, to_pid); // resend all unacked messages once
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
