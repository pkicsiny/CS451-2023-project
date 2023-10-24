#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

class Logger {
  public:
    const char* output_path;
    std::ostringstream ss;

    Logger ();
    Logger (const char*);

    int log_delivery(){
    
      // log into
      std::ofstream output_file;
      output_file.open(output_path, std::ios_base::app);
    
      if (output_file.is_open()){
        output_file << ss.str();
        output_file.close();
        return 0;
      }else{
        std::cout << "Could not open output file: " << output_path << std::endl;
        return -1;
      }
    }

};

Logger::Logger(){
}

Logger::Logger(const char* op){
  output_path = op;
}
