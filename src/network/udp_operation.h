
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include <string>

#include "logger.h"

class UDPOperation {
 private:
  int fd_;
  const char* remote_host_;
  const int remote_port_;
  const char* interface_;
  struct sockaddr_in cliaddr_;

 public:
  UDPOperation(const char* remote_host, const int remote_port, const char* interface);
  ~UDPOperation();

  bool create_server();
  bool create_client();
  void destory();
  int get_ifaddr(char* addr);
  bool send_buffer(char* buffer, size_t size);
  int recv_buffer(char* buffer, size_t size);
};
