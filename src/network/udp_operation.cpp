#include "udp_operation.h"

UDPOperation::UDPOperation(const char* remote_host, const int remote_port, const char* interface)
    : fd_(-1), remote_host_(remote_host), remote_port_(remote_port), interface_(interface) {
  memset(&(this->cliaddr_), 0, sizeof(sockaddr_in));
  this->cliaddr_.sin_family = AF_INET;
  this->cliaddr_.sin_port = htons(this->remote_port_);  // 接收端需要绑定remote_port端口
}

UDPOperation::~UDPOperation() {}

bool UDPOperation::create_server() {
  this->fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (this->fd_ == -1) {
    MLOG_ERROR("Socket creation failed: %s", strerror(errno));
    throw std::runtime_error("Socket creation failed");
  }

  inet_pton(AF_INET, this->remote_host_, &this->cliaddr_.sin_addr.s_addr);

  hostent* host = gethostbyname(remote_host_);
  unsigned long hostip = *(unsigned long*)host->h_addr;

  this->cliaddr_.sin_addr.s_addr = hostip;

  unsigned char net = hostip & 0xff;
  if (net > 223 && net < 240)  // 如果是多播
  {
    char numeric_ip[32] = "\0";
    get_ifaddr(numeric_ip);
    struct in_addr outputif;
    outputif.s_addr = inet_addr(numeric_ip);
    MLOG_DEBUG("interface = %s, numeric_ip = %s", interface_, numeric_ip);
    if (setsockopt(this->fd_, IPPROTO_IP, IP_MULTICAST_IF, (char*)&outputif, sizeof(struct in_addr))) {
      throw std::runtime_error("Socket setsockopt failed");
    }
  }

  return true;
}

bool UDPOperation::create_client() {
  this->fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (this->fd_ == -1) {
    MLOG_ERROR("Socket creation failed: %s", strerror(errno));
    throw std::runtime_error("Socket creation failed");
  }

  // 设置socket选项，允许重用地址
  int reuse = 1;
  if (setsockopt(this->fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    MLOG_ERROR("Error setting socket option: %s", strerror(errno));
    this->destory();
    throw std::runtime_error("Socket setsockopt failed");
  }

  struct sockaddr_in local_addr;  // local address
  memset(&local_addr, 0, sizeof(local_addr));
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = inet_addr("0.0.0.0");  // 设定本地监听必须是0.0.0.0 这里是关键！
  local_addr.sin_port = htons(remote_port_);          // this port must be the group port
  // 建立本地绑定（主机地址/端口号）
  if (bind(this->fd_, (struct sockaddr*)&local_addr, sizeof(local_addr)) != 0) {
    MLOG_ERROR("Error binding socket: %s", strerror(errno));
    this->destory();
    throw std::runtime_error("Socket bind failed");
  }

  // 如果是组播 加入组播
  int net = stoi(std::string(remote_host_).substr(0, std::string(remote_host_).find('.')));
  if (net >= 224 && net <= 239) {
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(this->remote_host_);
    if (strlen(interface_) == 0) {
      mreq.imr_interface.s_addr = htonl(INADDR_ANY);  // 任意接口接收组播信息
    } else {
      char numeric_ip[32] = "\0";
      get_ifaddr(numeric_ip);
      MLOG_DEBUG("interface = %s, numeric_ip = %s", interface_, numeric_ip);
      mreq.imr_interface.s_addr = inet_addr(numeric_ip);  // 指定新接口接收组播信息
    }

    if (setsockopt(this->fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
      MLOG_ERROR("Error setting socket option for multicast: %s", strerror(errno));
      this->destory();
      throw std::runtime_error("Socket setsockopt failed");
    }
  }
  return true;
}

int UDPOperation::get_ifaddr(char* addr) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, interface_);
  if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
    close(sock);
    throw std::runtime_error("Socket get_ifaddr failed");
    return 1;
  }

  strcpy(addr, inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr));
  close(sock);

  return 0;
}

void UDPOperation::destory() { close(this->fd_); }

bool UDPOperation::send_buffer(char* buffer, size_t size) {
  socklen_t len = sizeof(struct sockaddr_in);
  // 数据广播
  int t = sendto(this->fd_, buffer, size, 0, (struct sockaddr*)&cliaddr_, len);
  if (t == -1) {
    this->destory();
    MLOG_ERROR("Socket send failed: %s", strerror(errno));
    throw std::runtime_error("Socket send_buffer failed");
  }
  return true;
}

int UDPOperation::recv_buffer(char* buffer, size_t size) {
  socklen_t len = sizeof(struct sockaddr_in);
  int bytes_received = recvfrom(this->fd_, buffer, size, 0, (struct sockaddr*)&this->cliaddr_, &len);
  if (bytes_received < 0) {
    this->destory();
    MLOG_ERROR("Error receiving data: %s", strerror(errno));
    throw std::runtime_error("Socket recv_buffer failed");
  }
  return bytes_received;
}
