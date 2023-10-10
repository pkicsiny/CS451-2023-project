#include <chrono>
#include <iostream>
#include <thread>

#include "parser.hpp"
#include "hello.h"
#include <signal.h>

// I load these

#include <fstream>
#include <string>

#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h> 
#define _XOPEN_SOURCE_EXTENDED 1

#include <map>


static void stop(int) {
  // reset signal handlers to default
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  // immediately stop network packet processing
  std::cout << "Immediately stopping network packet processing.\n";

  // write/flush output file if necessary
  std::cout << "Writing output.\n";

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

  /*------------------------*/
  // init process-port dict //
  /*------------------------*/

  std::map<int64_t, int64_t> port_proc_dict;
  for (auto &host : hosts) {
    port_proc_dict[host.port] = host.id;
    std::cout << "port: " << host.port << ": process ID " << port_proc_dict[host.port] << std::endl;
  }

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
      std::cout << "[ERROR] bind()" << bind_return << std::endl;
      return -1;     
    }
    
    // client address
    sockaddr_in client_addr;
    socklen_t client_addr_size  = sizeof(client_addr);
    char msg_buf[128];  // buffer for messages

    // wait for messages incoming to server's port
    std::cout << "Waiting to receive messages..." << std::endl;
    while(true){
      int64_t msg_recv = recvfrom(socket_fd, msg_buf, sizeof(msg_buf), 0,
          reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_size);  // returns length of incoming message

      if (msg_recv < 0) {
          std::cout << "Receive failed with error" << std::endl;
          return -1;
      }else if (msg_recv != 0) {
          msg_buf[msg_recv] = '\0'; //end of line to truncate junk
          std::cout << "Received " << msg_recv << " characters successfully." << std::endl;
          printf("Received message: %s\n", msg_buf);
         
          int client_port = ntohs(client_addr.sin_port); 
          std::cout << "Client address port: " << client_port << std::endl;

          // write to output
          std::ofstream output_file;
          output_file.open(parser.outputPath(), std::ios_base::app);
          if (output_file.is_open()){
            output_file << "d " << port_proc_dict[client_port] << " " << msg_buf << std::endl;
            output_file.close();
          }else{
            std::cout << "Could not open output file: " << parser.outputPath() << std::endl;
            return -1;
          }
      } // if
    } // while
 
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
    std::cout << "server port: " << htons(my_port) << " " << client_addr.sin_port <<  std::endl;
    if (int64_t bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr)) < 0){
      std::cout << "[ERROR] bind()" << bind_return << std::endl;
      return -1;     
    }

    std::cout << "Starting sending messages..." << std::endl;
    for (int msg_i=1; msg_i<=config_m; msg_i++){

      std::string msg_i_str = std::to_string(msg_i);
      std::cout << "Sending message: " << msg_i_str << " from port " << my_port << " of length: " << msg_i_str.length() << std::endl;
      
      // send to server
      int64_t msg_send = sendto(socket_fd, msg_i_str.c_str(), msg_i_str.length(), 0,
          reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));  // returns number of characters sent
      if (msg_send < 0) {
          std::cout << "Send failed with error" << std::endl;
          return -1;
      }else{
          std::cout << "Send successful: " << msg_send << std::endl;

          // write broadcast event output
          std::ofstream output_file;
          output_file.open(parser.outputPath(), std::ios_base::app);
          if (output_file.is_open()){
            output_file << "b " <<  msg_i_str << std::endl;
            output_file.close();
          }else{
            std::cout << "Could not open output file: " << parser.outputPath() << std::endl;
            return -1;
          }













      }
    } // end for
  } // end if

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    
    //std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
