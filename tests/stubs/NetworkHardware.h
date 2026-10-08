#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

constexpr int NSAPI_ERROR_WOULD_BLOCK = -3001;
constexpr int NSAPI_SECURITY_NONE = 0;
constexpr int NSAPI_SECURITY_WPA_WPA2 = 1;
constexpr int NSAPI_IPv4 = 4;
constexpr int Station = 0;

class SocketAddress {
  public:
  unsigned int port = 0;
  const char *get_ip_address() const {
    return "192.0.2.1";
  }
  void set_port(uint16_t value) {
    port = value;
  }
};

class NetworkInterface {
  public:
  const char *get_ip_address();
  int gethostbyname(const char *host, SocketAddress *address, int version);
};

class TCPSocket {
  public:
  bool opened = false;
  unsigned int port = 0;
  size_t received = 0;
  std::string sent;
  ~TCPSocket();
  int open(NetworkInterface *network);
  void set_timeout(int value);
  int connect(const SocketAddress &address);
  int close();
  int send(const void *data, size_t size);
  int recv(void *data, size_t size);
};
