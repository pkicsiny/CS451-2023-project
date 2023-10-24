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

#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h> 
#define _XOPEN_SOURCE_EXTENDED 1

#include <map>

Logger logger_p2p;

static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  // immediately stop network packet processing
  std::cout << "Immediately stopping network packet processing.\n";

  // write/flush output file if necessary
  std::cout << "Writing output.\n";
  std::cout << logger_p2p.ss.str() << std::endl;
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

  /*------------------------*/
  // init process-port dict //
  /*------------------------*/

  std::map<int64_t, int64_t> port_proc_dict;
  for (auto &host : hosts) {
    port_proc_dict[host.port] = host.id;
    std::cout << "port: " << host.port << ": process ID " << port_proc_dict[host.port] << std::endl;
  }

  /*------------------------------------*/
  // init process-received message dict //
  /*------------------------------------*/

  std::map<int64_t, std::vector<std::string>> proc_recv_dict;

  /*------------------*/
  // read config file //
  /*------------------*/

  std::string l_in;
  std::ifstream config_file;
  std::vector<int> config_file_content_vec;
  config_file.open (parser.configPath());
  int config_m = -1, config_i = -1;
  if (config_file.is_open()){
    while (getline(config_file, l_in, ' ')){
      std::cout << l_in << std::endl;
      config_file_content_vec.push_back(std::stoi(l_in));
    }
    config_m = config_file_content_vec[0];
    config_i = config_file_content_vec[1];
    config_file.close();
  }else{
    std::cout << "Could not open config file: " << parser.configPath() << std::endl;
  }

  if (config_m == -1 || config_i == -1){
    std::cout << "Error in reading config. config_m: " << config_m << ", config_i: " << config_i << std::endl;
  }else{
    std::cout << "Config successfully read. Send " << config_m << " messages to process " << config_i << std::endl;
  }

  // set this process IP and port
  char my_ip[200];
  sprintf(my_ip, "%.10s", hosts[parser.id()-1].ipReadable().c_str());
  int my_port = hosts[parser.id()-1].port;
  std::cout << "my IP:" << my_ip << ", my port:" << my_port << std::endl;

  // set server IP and port
  char serv_ip[200];
  sprintf(serv_ip, "%.10s", hosts[config_i-1].ipReadable().c_str());
  int serv_port = hosts[config_i-1].port;
  std::cout << "server IP:" << serv_ip << ", server port:" << serv_port << std::endl;

  /*-------------------*/
  // init message list //
  /*-------------------*/

  std::vector<int> msg_list(config_m);
  for (int i=0; i<config_m; i++){
    msg_list[i] = i+1;
  }
  //std::cout << "message list:" << std::endl;
  //for (auto const& msg_i : msg_list){
  //  std::cout << msg_i << std::endl;
  //}

  /*-------------------------*/
  // init pid msg count dict //
  /*-------------------------*/
  
  std::map<int64_t, std::map<int64_t, int64_t> > pid_msg_count_dict;
  for (auto &host : hosts) {
    for (auto const& msg_i : msg_list){
      pid_msg_count_dict[host.port][msg_i] = 0;
    }
  }

  // recv timeout
  struct timeval read_timeout;
  read_timeout.tv_sec = 0;
  read_timeout.tv_usec = 10;


  /*---------------*/
  // create socket //
  /*---------------*/

  int socket_fd;
  struct sockaddr_in serv_addr;

  std::cout << "Broadcasting and delivering messages...\n\n";

  // on senders (clients): create a socket and bind (assign address) this socket to the receiver
  // on receiver (server): create a socket and listen for incoming events
  int64_t my_pid = parser.id();

  /*--------*/
  // server //
  /*--------*/

  if (my_pid == config_i){
    std::cout << "---I am the server---" << std::endl;

    // AF_INET: IPv4, SOCK_DGRAM: UDP/IP
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
        std::cout << "[ERROR] socket()" << socket_fd << std::endl;
        return -1; 
    }
                                                                                                                     
    // zero out the structure
    //std::memset(reinterpret_cast<char *>(&sockaddr_in), 0, sizeof(sockaddr_in));
                                                                                                                     
    /* setup the host_addr structure for use in bind call */
    // server byte order
    serv_addr.sin_family = AF_INET; 
                                                                                                                     
    // automatically be filled with current host's IP address
    serv_addr.sin_addr.s_addr = INADDR_ANY; //inet_addr(serv_ip); //INADDR_ANY;  
                                                                                                                     
    // convert short integer value for port must be converted into network byte order
    serv_addr.sin_port = htons(serv_port);  // port of server process
    std::cout << "server port: " << htons(serv_port) << " " << serv_addr.sin_port <<  std::endl;
                                                                                                                     
    // bind socket to server address
    if (int64_t bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0){
      std::cout << "[ERROR] bind(): " << bind_return << std::endl;
      return -1;     
    }
    
    // client address
    sockaddr_in client_addr;
    socklen_t client_addr_size  = sizeof(client_addr);
    char msg_buf[128];  // buffer for messages
    std::ostringstream ss_recv;

    // wait for messages incoming to server's port
    std::cout << "Waiting to receive messages..." << std::endl;
    while(true){

      // blocking recv
      int64_t msg_recv = recvfrom(socket_fd, msg_buf, sizeof(msg_buf), 0,
          reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_size);  // returns length of incoming message
      if (msg_recv < 0) {
        std::cout << "Receive failed with error" << std::endl;
      }else if (msg_recv != 0) {
        msg_buf[msg_recv] = '\0'; //end of line to truncate junk
        int client_port = ntohs(client_addr.sin_port); 
        //std::cout << "Successfully received message: " << msg_buf << ", having length " << msg_recv << ", from port: " << client_port << std::endl;

        // upon successful receive trigger event send ack once, ack is "port msg"
        std::string msg_ack = std::to_string(client_port) + " " + msg_buf;
        //std::cout << "Sending ack message: " << msg_ack << ", of length: " << msg_ack.length() << ", from port: " << my_port << ", to port: " << client_port << std::endl;
        int64_t msg_ack_send = sendto(socket_fd, msg_ack.c_str(), msg_ack.length(), 0,
            reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr));  // returns number of characters sent
        if (msg_ack_send < 0) {
            std::cout << "Sending ack message " << msg_ack << " failed with error" << std::endl;
            return -1;
        }else{
            //std::cout << "[Send ack successful]" << std::endl;
        }

        // if message is duplicate, do not store in log (if not in dict add to it)
        int64_t client_pid = port_proc_dict[client_port];

        // pid is not in dict i.e. this is the first msg from proc pid
        if (proc_recv_dict.find(client_pid) == proc_recv_dict.end()) {
          proc_recv_dict[client_pid].push_back(msg_buf);
          log_deliver("test_out.txt", 'd', client_pid, msg_buf);
          logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg_buf << '\n';

        // pid is already in dict i.e. msg might be a duplicate
        } else {

          // if this is true msg_buf is not yet in dict[pid]
          if (std::find(proc_recv_dict[client_pid].begin(), proc_recv_dict[client_pid].end(), msg_buf) == proc_recv_dict[client_pid].end()){
            // msg is not yet in dict so log it
            proc_recv_dict[client_pid].push_back(msg_buf);
            log_deliver("test_out.txt", 'd', client_pid, msg_buf);
            logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg_buf << '\n';

          } // end if
        } // end if

        // print received messages
        //std::cout << "Received messages:" << std::endl;
        //for (auto const &pair: proc_recv_dict) {
        //  std::cout << "PID " << pair.first << ": [";
        //  for (auto const& i: pair.second){
        //    std::cout << i << ", ";
        //  }
        //  std::cout << "]" << std::endl;
        //}

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
    if (int64_t bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr)) < 0){
      std::cout << "[ERROR] bind(): " << bind_return << std::endl;
      return -1;     
    }

    // set timeout on socket
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

    char ack_buf[128];  // buffer for messages
    socklen_t serv_addr_size  = sizeof(serv_addr);
    std::ostringstream ss_send;

    std::cout << "Start sending messages..." << std::endl;
    while(!msg_list.empty()){ 
      //std::cout << "Messages in message list to send: [";
      //for (auto const& i : msg_list){
      //  std::cout << i << ", ";
      //}
      //std::cout << "]" << std::endl;

      for (auto const& msg_i : msg_list){  // loop over messages
        // send each message in a while loop that terminates only when client receives the ack from server
        std::string msg_i_str = std::to_string(msg_i);
        //std::cout << "Sending message: " << msg_i_str << ", of length: " << msg_i_str.length() << ", from port: " << my_port << ", to port: " << serv_port << std::endl;

        int64_t msg_send = sendto(socket_fd, msg_i_str.c_str(), msg_i_str.length(), 0,
            reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));  // returns number of characters sent
        
        // log broadcast after first send
        if (pid_msg_count_dict[serv_port][msg_i] == 0){
          //log_broadcast(parser.outputPath(), msg_i_str);
          logger_p2p.ss << 'b' << ' ' << msg_i_str << '\n';
        }
        
        // increment sent msg_i to pid once more
        pid_msg_count_dict[serv_port][msg_i] += 1;

        if (msg_send < 0) {
            std::cout << "[Send ERROR]" << std::endl;
        }//else{
            //std::cout << "[Send successful]" << std::endl;
        //} // end if
      } // end for

      // listen to acks, ack is a tuple of (pdi, msg)
      //std::cout << "Listening to acks..." << std::endl;
      while(true){

        // this is blocking so it listens indefinitely; make it nonblocking by setting a timeout
        int64_t ack_recv = recvfrom(socket_fd, ack_buf, sizeof(ack_buf), 0,
            reinterpret_cast<struct sockaddr *>(&serv_addr), &serv_addr_size);  // returns length of incoming message
        if (ack_recv < 0) {
          break; // terminate while loop i.e. no more ack to receive
        }else if (ack_recv != 0) {
          ack_buf[ack_recv] = '\0'; //end of line to truncate junk
          int serv_port = ntohs(serv_addr.sin_port); 
          //std::cout << "Successfully received ack message: " << ack_buf << ", having length " << ack_recv << ", from port: " << serv_port << std::endl;
          // split string
          std::istringstream ss(ack_buf);
          std::string word;
          std::vector<int> ack_vec;
    
          while (std::getline(ss, word, ' ')) {
            ack_vec.push_back(std::stoi(word));
          }
  
          // get index of msg in msg_list
          uint64_t msg_list_idx = std::distance(msg_list.begin(), std::find(msg_list.begin(), msg_list.end(), ack_vec[1]));

          if(msg_list_idx >= msg_list.size()) {
            //std::cout << "Ack msg " << ack_vec[1] << " not in msg_list" << std::endl;
          } else {
            // remove acked msg from msg_list
            msg_list.erase(msg_list.begin()+msg_list_idx);
            //std::cout << "msg_list after removing acked " << ack_vec[1] << ": [";
            //for (auto const& i : msg_list){
            //  std::cout << i << ", ";
            //}
            //std::cout << "]" << std::endl;
          }  // end if
        }  // end if (ack_recv < 0)
      }  // end while recv

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
