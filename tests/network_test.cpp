#include "stubs/DiagnosticHardware.h"
#include <EEPROMInterface.h>
#include <EMW10xxInterface.h>
#include <SystemWiFi.h>
#include <SystemTime.h>
#include <mbedtls/ssl.h>
#include "AppConfig.h"
#include "NetworkTestConfig.h"
#include "OnboardTests.h"
#include <algorithm>

namespace {
  std::string failure;
  EMW10xxInterface wifi;
  mbedtls_x509_crt peer;
  unsigned int interfaces = 0;
  unsigned int connections = 0;
  int openSockets = 0;
  bool missingClock = false;

  int result(const char *stage) {
    return failure == stage ? -42 : 0;
  }
}

bool InitSystemWiFi() {
  ++interfaces;
  return failure != "interface";
}
NetworkInterface *WiFiInterface() {
  return &wifi;
}
int EEPROMInterface::readWiFiSetting(char *ssid, int ssidSize, char *password, int passwordSize) {
  check(ssidSize == 33 && passwordSize == 65, "bounded credential buffers");
  strcpy(ssid, "test-fixture");
  strcpy(password, "test-fixture-password");
  return result("credentials");
}
int EMW10xxInterface::set_interface(int mode) {
  check(mode == Station, "station mode only");
  return result("station");
}
int EMW10xxInterface::connect(const char *ssid, const char *password, int security, int channel) {
  check(std::string(ssid) == "test-fixture" && std::string(password) == "test-fixture-password" && security == NSAPI_SECURITY_WPA_WPA2 && channel == 0, "use saved credentials, not compiled actual secrets");
  ++connections;
  return result("wifi");
}
const char *NetworkInterface::get_ip_address() {
  return failure == "dhcp" ? "0.0.0.0" : "192.0.2.2";
}
int NetworkInterface::gethostbyname(const char *host, SocketAddress *, int version) {
  check(std::string(host) == NetworkTestConfig::kHost && version == NSAPI_IPv4, "explicit test-endpoint IPv4 DNS");
  return result("dns");
}
void SyncTime() {}
int IsTimeSynced() {
  return missingClock ? -1 : 0;
}
TCPSocket::~TCPSocket() {
  if (opened) {
    close();
  }
}
int TCPSocket::open(NetworkInterface *network) {
  check(network == &wifi, "socket uses saved-network interface");
  if (failure == "open") {
    return -42;
  }
  opened = true;
  ++openSockets;
  return 0;
}
void TCPSocket::set_timeout(int value) {
  check(value == 1000, "bounded socket operations");
}
int TCPSocket::connect(const SocketAddress &address) {
  port = address.port;
  check(port == 80 || port == 443, "only HTTP/HTTPS test ports");
  return result(port == 80 ? "tcp80" : "tcp443");
}
int TCPSocket::close() {
  if (opened) {
    --openSockets;
    opened = false;
  }
  return result("close");
}
int TCPSocket::send(const void *data, size_t size) {
  if (failure == "send-timeout") {
    return NSAPI_ERROR_WOULD_BLOCK;
  }
  if (failure == "send") {
    return -42;
  }
  const size_t count = std::min(size, static_cast<size_t>(7));
  sent.append(static_cast<const char *>(data), count);
  return static_cast<int>(count);
}
int TCPSocket::recv(void *data, size_t size) {
  check(sent.find("Host: " + std::string(NetworkTestConfig::kHost)) != std::string::npos && sent.find("password") == std::string::npos && sent.find("test-fixture") == std::string::npos, "only generic HTTP request leaves the board");
  if (failure == "receive-timeout") {
    return NSAPI_ERROR_WOULD_BLOCK;
  }
  if (failure == "receive") {
    return -42;
  }
  std::string text = port == 80 ? "HTTP/1.1 301 Redirect\r\nContent-Length: 0\r\n\r\n" : "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
  if (failure == "status") {
    text = "HTTP/1.1 500 Error\r\n\r\n";
  } else if (failure == "headers") {
    text = "HTTP/1.1 200 OK\r\n";
  }
  const size_t count = std::min(std::min(size, static_cast<size_t>(5)), text.size() - received);
  memcpy(data, text.data() + received, count);
  received += count;
  return static_cast<int>(count);
}

void mbedtls_entropy_init(mbedtls_entropy_context *) {}
void mbedtls_entropy_free(mbedtls_entropy_context *) {}
int mbedtls_entropy_func(void *, unsigned char *, size_t) {
  return 0;
}
void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *) {}
void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *) {}
int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *, int (*)(void *, unsigned char *, size_t), void *, const unsigned char *, size_t) {
  return result("entropy");
}
int mbedtls_ctr_drbg_random(void *, unsigned char *, size_t) {
  return 0;
}
void mbedtls_x509_crt_init(mbedtls_x509_crt *crt) {
  *crt = mbedtls_x509_crt();
}
void mbedtls_x509_crt_free(mbedtls_x509_crt *) {}
int mbedtls_x509_crt_parse(mbedtls_x509_crt *crt, const unsigned char *pem, size_t size) {
  check(size == sizeof(NetworkTestConfig::kRootCa) && memcmp(pem, NetworkTestConfig::kRootCa, size) == 0, "configured public trust anchor");
  if (failure == "expired-root") {
    crt->valid_to.year = 2010;
  }
  return result("ca");
}
int mbedtls_x509_crt_verify(mbedtls_x509_crt *crt, mbedtls_x509_crt *, void *, const char *host, uint32_t *flags, TestVerifyCallback callback, void *context) {
  check(std::string(host) == "invalid.invalid", "negative hostname verification");
  *flags = failure == "negative-hostname" ? 0 : MBEDTLS_X509_BADCERT_CN_MISMATCH;
  callback(context, crt, 0, flags);
  return *flags == 0 ? 0 : -42;
}
void mbedtls_ssl_config_init(mbedtls_ssl_config *config) {
  *config = mbedtls_ssl_config();
}
void mbedtls_ssl_config_free(mbedtls_ssl_config *) {}
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *, int, int, int) {
  return result("config");
}
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *config, int value) {
  config->authmode = value;
}
void mbedtls_ssl_conf_min_version(mbedtls_ssl_config *config, int major, int minor) {
  check(major == 3, "TLS protocol major");
  config->minorVersion = minor;
}
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *config, mbedtls_x509_crt *root, void *) {
  config->root = root;
}
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *, int (*)(void *, unsigned char *, size_t), void *) {}
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *config, TestVerifyCallback callback, void *context) {
  config->verify = callback;
  config->verifyContext = context;
}
void mbedtls_ssl_init(mbedtls_ssl_context *ssl) {
  *ssl = mbedtls_ssl_context();
}
void mbedtls_ssl_free(mbedtls_ssl_context *) {}
int mbedtls_ssl_setup(mbedtls_ssl_context *ssl, const mbedtls_ssl_config *config) {
  ssl->config = config;
  return result("setup");
}
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *ssl, const char *host) {
  ssl->host = host;
  return result("hostname");
}
void mbedtls_ssl_set_bio(mbedtls_ssl_context *ssl, void *socket, int (*send)(void *, const unsigned char *, size_t), int (*receive)(void *, unsigned char *, size_t), void *) {
  ssl->socket = socket;
  ssl->send = send;
  ssl->receive = receive;
}
int mbedtls_ssl_handshake(mbedtls_ssl_context *ssl) {
  check(ssl->config->authmode == MBEDTLS_SSL_VERIFY_REQUIRED && ssl->config->minorVersion == 3, "mandatory verification, minimum TLS 1.2");
  check(ssl->host == NetworkTestConfig::kHost || std::string(ssl->host) == NetworkTestConfig::kHost, "SNI/hostname validation target");
  check(ssl->config->verify != nullptr, "explicit date checks despite old SDK configuration");
  if (failure == "tls-timeout") {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  peer = mbedtls_x509_crt();
  if (failure == "expired-leaf") {
    peer.valid_to.year = 2010;
  } else if (failure == "future-leaf") {
    peer.valid_from.year = 2040;
  }
  ssl->config->verify(ssl->config->verifyContext, &peer, 0, &ssl->flags);
  ssl->config->verify(ssl->config->verifyContext, ssl->config->root, 1, &ssl->flags);
  if (failure == "flags") {
    ssl->flags |= MBEDTLS_X509_BADCERT_CN_MISMATCH;
  }
  return ssl->flags != 0 ? -42 : result("handshake");
}
uint32_t mbedtls_ssl_get_verify_result(mbedtls_ssl_context *ssl) {
  return ssl->flags;
}
const mbedtls_x509_crt *mbedtls_ssl_get_peer_cert(mbedtls_ssl_context *) {
  return failure == "peer" ? nullptr : &peer;
}
int mbedtls_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *data, size_t size) {
  return ssl->send(ssl->socket, data, size);
}
int mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *data, size_t size) {
  return ssl->receive(ssl->socket, data, size);
}

int main() {
  NetworkTests::run();
  if (!AppConfig::kNetworkServicesEnabled) {
    check(interfaces == 0 && connections == 0 && openSockets == 0, "inactive network diagnostics make no connection");
    std::cout << "PASS: network diagnostic exclusion\n";
    return 0;
  }
  check(Serial.output.find("PASS: DNS, TCP, HTTP, NTP") != std::string::npos && openSockets == 0, "positive network path and socket cleanup");
  for (const char *stage: {"interface",
         "credentials",
         "station",
         "wifi",
         "dhcp",
         "dns",
         "open",
         "tcp80",
         "tcp443",
         "send",
         "receive",
         "status",
         "headers",
         "send-timeout",
         "receive-timeout",
         "entropy",
         "ca",
         "config",
         "setup",
         "hostname",
         "handshake",
         "tls-timeout",
         "flags",
         "peer",
         "negative-hostname",
         "expired-leaf",
         "future-leaf",
         "expired-root",
         "close"}) {
    failure = stage;
    Serial.output.clear();
    NetworkTests::run();
    check(Serial.output.find("Network FAIL:") != std::string::npos && Serial.output.find("PASS: DNS, TCP, HTTP, NTP") == std::string::npos, stage);
    check(openSockets == 0, "sockets are released on every failure");
    check(Serial.output.find("test-fixture-password") == std::string::npos, "password never logged");
  }
  failure.clear();
  missingClock = true;
  Serial.output.clear();
  NetworkTests::run();
  check(Serial.output.find("refusing TLS without a current clock") != std::string::npos, "no TLS success without NTP");
  check(!Screen.invalidWrite, "network status fits OLED");
  std::cout << "PASS: exact HTTP/HTTPS statuses, partial I/O, required TLS verification, leaf/root dates, wrong-host rejection, bounded waits and all failure cleanup\n";
}
