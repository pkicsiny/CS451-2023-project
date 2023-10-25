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

  /*--------------------*/
  // init port-pid dict //
  /*--------------------*/

  std::map<int, int64_t> port_pid_dict;  // in parser: port: u16 bit, pid: u32 bit (could be u16)
  for (auto &host : hosts) {
    port_pid_dict[host.port] = host.id;
    std::cout << "port: " << host.port << ": process ID " << port_pid_dict[host.port] << std::endl;
  }

  /*------------------------------------*/
  // init process-received message dict //
  /*------------------------------------*/

  std::map<int64_t, std::vector<std::string>> pid_recv_dict;

  /*------------------*/
  // read config file //
  /*------------------*/

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
    }
  }else{  // if no config, read from file or something
  }

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

  /*-------------------*/
  // init message list //
  /*-------------------*/
  
  std::vector<Message> msg_list;

  // this bit is adapted to the config but msg_list could be filled up differently
  for (int i=0; i<NUM_MSG; i++){
    msg_list.push_back(Message(i, std::to_string(i+1)));  // sequencing starts from 0
    //msg_list[i] = std::to_string(i+1);
  }
  //std::cout << "message list:" << std::endl;
  //for (auto const& msg_i : msg_list){
  //  std::cout << msg_i << std::endl;
  //}

  /*-------------------------*/
  // init pid msg count dict //
  /*-------------------------*/
  
  // keeps track which msg has been sent to which process how many times
  std::map<int64_t, std::map<int, int> > pid_msg_count_dict;
  for (auto &host : hosts) {
    for (size_t i=0; i < msg_list.size(); i++){
    //for (auto const& msg_i : msg_list){
      int sn = msg_list[i].sn;
      pid_msg_count_dict[host.port][sn] = 0;
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
        //std::cout << "Successfully received message. port-msg: " << msg_buf << '-' << client_port << std::endl;

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
        int64_t client_pid = port_pid_dict[client_port];

        // pid is not in dict i.e. this is the first msg from proc pid
        if (pid_recv_dict.find(client_pid) == pid_recv_dict.end()) {
          pid_recv_dict[client_pid].push_back(msg_buf);
          //log_deliver(parser.outputPath(), 'd', client_pid, msg_buf);
          logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg_buf << '\n';

        // pid is already in dict i.e. msg might be a duplicate
        } else {

          // if this is true msg_buf is not yet in dict[pid]
          if (std::find(pid_recv_dict[client_pid].begin(), pid_recv_dict[client_pid].end(), msg_buf) == pid_recv_dict[client_pid].end()){
            // msg is not yet in dict so log it
            pid_recv_dict[client_pid].push_back(msg_buf);
            //log_deliver(parser.outputPath(), 'd', client_pid, msg_buf);
            // print ss here before and after this line to see each append is successful or not
            logger_p2p.ss << 'd' << ' ' << client_pid << ' ' << msg_buf << '\n';

          } // end if
        } // end if

        // print received messages
        //std::cout << "Received messages:" << std::endl;
        //for (auto const &pair: pid_recv_dict) {
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
    if (int bind_return = bind(socket_fd, reinterpret_cast<struct sockaddr *>(&client_addr), sizeof(client_addr)) < 0){
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
      std::cout << "Number of messags in list to send: " << msg_list.size() << std::endl;

      for (std::size_t msg_idx = 0; msg_idx < msg_list.size(); ++msg_idx){
        // send each message in a while loop that terminates only when client receives the ack from server
//        if (typeid(msg_list[msg_idx]).name()=="string"){
  //        std::string msg_i_str = std::to_string(msg_list[msg_idx]);  // just for safety
  //      }
        //std::cout << "Sending message: " << msg_i_str << ", of length: " << msg_i_str.length() << ", from port: " << my_port << ", to port: " << serv_port << std::endl;
        
        // get Message object (msg_idx!=msg.sn)
        Message msg = msg_list[msg_idx];

        // send message without sequence number
        int64_t msg_send = sendto(socket_fd, msg.msg.c_str(), msg.msg.length(), 0,
            reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));  // returns number of characters sent
        
        // log broadcast after first send
        if (pid_msg_count_dict[serv_port][msg.sn] == 0){
          //log_broadcast(parser.outputPath(), msg_i_str);
          logger_p2p.ss << 'b' << ' ' << msg.msg << '\n';
        }
        pid_msg_count_dict[serv_port][msg.sn] += 1;

        if (msg_send < 0) {
            std::cout << "[Send ERROR]" << std::endl;
        }
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

          // ack string is: "client_pid m" 
          std::istringstream ss(ack_buf);
          std::string word;
          std::vector<std::string> ack_vec;
    
          while (std::getline(ss, word, ' ')) {
            ack_vec.push_back(word);
          }
  
          // get index of msg in msg_list
          std::string acked_msg = ack_vec[1];
          auto acked_msg_it = std::find_if(msg_list.begin(), msg_list.end(), [&acked_msg](const Message& m) {return m.msg == acked_msg;});

          //int msg_list_idx = std::distance(msg_list.begin(), std::find(msg_list.begin(), msg_list.end(), ack_vec[1]));

          if (acked_msg_it != msg_list.end()) {
            auto acked_msg_idx = std::distance(msg_list.begin(), acked_msg_it);
//          if(msg_list_idx >= msg_list.size()) {
            //std::cout << "Ack msg " << ack_vec[1] << " not in msg_list" << std::endl;

            // remove acked msg from msg_list
            msg_list.erase(msg_list.begin()+acked_msg_idx);
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
