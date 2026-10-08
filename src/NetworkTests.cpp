#include <Arduino.h>
#include <EEPROMInterface.h>
#include <EMW10xxInterface.h>
#include <SystemTime.h>
#include <SystemWiFi.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <time.h>
#include "AppConfig.h"
#include "DiagnosticChecks.h"
#include "NetworkTestConfig.h"
#include "OnboardTests.h"
#include "SensitiveMemory.h"

namespace {
  namespace Config = NetworkTestConfig;

  bool fail(const char *stage, int code) {
    Serial.print(F("Network FAIL: "));
    Serial.print(stage);
    Serial.print(F(" ("));
    Serial.print(code);
    Serial.println(F(")."));
    Screen.print(1, "Network FAIL");
    Screen.print(2, "See USB serial");
    return false;
  }

  bool retry(int result) {
    return result == NSAPI_ERROR_WOULD_BLOCK || result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE;
  }

  int sendTls(void *context, const unsigned char *data, size_t size) {
    const int result = static_cast<TCPSocket *>(context)->send(data, size);
    return result == NSAPI_ERROR_WOULD_BLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : result;
  }

  int receiveTls(void *context, unsigned char *data, size_t size) {
    const int result = static_cast<TCPSocket *>(context)->recv(data, size);
    return result == NSAPI_ERROR_WOULD_BLOCK ? MBEDTLS_ERR_SSL_WANT_READ : result;
  }

  uint64_t timeKey(const mbedtls_x509_time &value) {
    return DiagnosticChecks::calendarKey(value.year, value.mon, value.day, value.hour, value.min, value.sec);
  }

  int verifyDates(void *context, mbedtls_x509_crt *certificate, int, uint32_t *flags) {
    // Core 2.0.0 disables MBEDTLS_HAVE_TIME_DATE; enforce validity on every chain certificate here.
    const uint64_t now = timeKey(*static_cast<const mbedtls_x509_time *>(context));
    if (now < timeKey(certificate->valid_from)) {
      *flags |= MBEDTLS_X509_BADCERT_FUTURE;
    }
    if (now > timeKey(certificate->valid_to)) {
      *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    }
    return 0;
  }

  class TlsSession {
    public:
    TCPSocket socket;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_x509_crt root;
    mbedtls_ssl_config config;
    mbedtls_ssl_context ssl;

    TlsSession() {
      mbedtls_entropy_init(&entropy);
      mbedtls_ctr_drbg_init(&random);
      mbedtls_x509_crt_init(&root);
      mbedtls_ssl_config_init(&config);
      mbedtls_ssl_init(&ssl);
    }

    ~TlsSession() {
      mbedtls_ssl_free(&ssl);
      mbedtls_ssl_config_free(&config);
      mbedtls_x509_crt_free(&root);
      mbedtls_ctr_drbg_free(&random);
      mbedtls_entropy_free(&entropy);
    }
  };

  bool request(TCPSocket &socket, mbedtls_ssl_context *tls, int expectedStatus) {
    char requestText[256];
    const int length = snprintf(requestText, sizeof(requestText), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: MXCHIP-Diagnostics\r\n\r\n", Config::kPath, Config::kHost);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(requestText)) {
      return fail("HTTP request exceeds buffer", length);
    }
    const uint32_t startedMs = millis();
    size_t sent = 0;
    while (sent < static_cast<size_t>(length)) {
      const int result = tls == nullptr ? socket.send(requestText + sent, length - sent) : mbedtls_ssl_write(tls, reinterpret_cast<const unsigned char *>(requestText + sent), length - sent);
      if (static_cast<uint32_t>(millis()) - startedMs >= Config::kIoTimeoutMs) {
        return fail("HTTP send timeout", result);
      }
      if (retry(result)) {
        delay(10);
      } else if (result <= 0 || static_cast<size_t>(result) > length - sent) {
        return fail("HTTP send", result);
      } else {
        sent += static_cast<size_t>(result);
      }
    }
    DiagnosticChecks::HttpHeaders headers;
    const uint32_t receiveStartedMs = millis();
    while (static_cast<uint32_t>(millis()) - receiveStartedMs < Config::kIoTimeoutMs) {
      uint8_t buffer[128];
      const int received = tls == nullptr ? socket.recv(buffer, sizeof(buffer)) : mbedtls_ssl_read(tls, buffer, sizeof(buffer));
      if (retry(received)) {
        delay(10);
        continue;
      }
      if (received <= 0 || static_cast<size_t>(received) > sizeof(buffer)) {
        return fail("HTTP receive before complete headers", received);
      }
      for (int index = 0; index < received; ++index) {
        const DiagnosticChecks::HttpHeaders::Result result = headers.push(buffer[index]);
        if (result == DiagnosticChecks::HttpHeaders::Result::Invalid || result == DiagnosticChecks::HttpHeaders::Result::TooLong) {
          return fail("invalid/oversized HTTP headers", static_cast<int>(result));
        }
        if (result == DiagnosticChecks::HttpHeaders::Result::Complete) {
          if (headers.status() != expectedStatus) {
            return fail("unexpected HTTP status; redirects are not followed", headers.status());
          }
          Serial.print(tls == nullptr ? F("PASS: HTTP complete headers, status ") : F("PASS: HTTPS complete headers, status "));
          Serial.println(headers.status());
          return true;
        }
      }
    }
    return fail("HTTP response timeout", -1);
  }

  bool plainHttp(NetworkInterface *network, SocketAddress address) {
    Screen.print(1, "TCP / HTTP");
    TCPSocket socket;
    int result = socket.open(network);
    if (result != 0) {
      return fail("TCP open", result);
    }
    socket.set_timeout(1000);
    address.set_port(80);
    result = socket.connect(address);
    if (result != 0) {
      return fail("TCP port 80 connect", result);
    }
    Serial.println(F("PASS: TCP connection to port 80."));
    const bool ok = request(socket, nullptr, Config::kHttpStatus);
    const int closed = socket.close();
    if (closed != 0) {
      return fail("TCP close", closed);
    }
    return ok;
  }

  bool https(NetworkInterface *network, SocketAddress address, mbedtls_x509_time &now) {
    Screen.print(1, "TLS / HTTPS");
    TlsSession session;
    const unsigned char label[] = "MXCHIP diagnostics";
    int result = mbedtls_ctr_drbg_seed(&session.random, mbedtls_entropy_func, &session.entropy, label, sizeof(label) - 1);
    if (result != 0) {
      return fail("TLS entropy seed", result);
    }
    result = mbedtls_x509_crt_parse(&session.root, reinterpret_cast<const unsigned char *>(Config::kRootCa), sizeof(Config::kRootCa));
    if (result != 0) {
      return fail("parse configured trust anchor", result);
    }
    result = mbedtls_ssl_config_defaults(&session.config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
      return fail("TLS configuration", result);
    }
    mbedtls_ssl_conf_authmode(&session.config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_version(&session.config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_ca_chain(&session.config, &session.root, nullptr);
    mbedtls_ssl_conf_rng(&session.config, mbedtls_ctr_drbg_random, &session.random);
    mbedtls_ssl_conf_verify(&session.config, verifyDates, &now);
    result = mbedtls_ssl_setup(&session.ssl, &session.config);
    if (result != 0) {
      return fail("TLS setup", result);
    }
    result = mbedtls_ssl_set_hostname(&session.ssl, Config::kHost);
    if (result != 0) {
      return fail("TLS hostname", result);
    }
    result = session.socket.open(network);
    if (result != 0) {
      return fail("TLS TCP open", result);
    }
    session.socket.set_timeout(1000);
    address.set_port(443);
    result = session.socket.connect(address);
    if (result != 0) {
      return fail("TCP port 443 connect", result);
    }
    mbedtls_ssl_set_bio(&session.ssl, &session.socket, sendTls, receiveTls, nullptr);
    const uint32_t startedMs = millis();
    do {
      result = mbedtls_ssl_handshake(&session.ssl);
      if (static_cast<uint32_t>(millis()) - startedMs >= Config::kHandshakeTimeoutMs) {
        return fail("TLS handshake timeout", result);
      }
      if (retry(result)) {
        delay(10);
      }
    } while (retry(result));
    const uint32_t flags = mbedtls_ssl_get_verify_result(&session.ssl);
    if (result != 0 || flags != 0) {
      Serial.print(F("TLS verification flags: "));
      Serial.println(static_cast<unsigned long>(flags));
      return fail("TLS certificate/handshake", result);
    }
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&session.ssl);
    if (peer == nullptr) {
      return fail("TLS returned no peer certificate", -1);
    }
    uint32_t wrongHostFlags = 0;
    // The verifier doesn't modify the certificate, but this SDK's API is not const-correct.
    result = mbedtls_x509_crt_verify(const_cast<mbedtls_x509_crt *>(peer), &session.root, nullptr, "invalid.invalid", &wrongHostFlags, verifyDates, &now);
    if (result == 0 || (wrongHostFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) == 0) {
      return fail("negative hostname-verification check", result);
    }
    Serial.println(F("PASS: TLS trust chain, hostname and validity dates; incorrect hostname rejected."));
    const bool ok = request(session.socket, &session.ssl, Config::kHttpsStatus);
    const int closed = session.socket.close();
    if (closed != 0) {
      return fail("TLS TCP close", closed);
    }
    return ok;
  }

  bool connectSaved(NetworkInterface *&network) {
    if (!InitSystemWiFi()) {
      return fail("Wi-Fi interface initialization", -1);
    }
    network = WiFiInterface();
    if (network == nullptr) {
      return fail("Wi-Fi interface unavailable", -1);
    }
    char ssid[WIFI_SSID_MAX_LEN + 1] = {};
    char password[WIFI_PWD_MAX_LEN + 1] = {};
    EEPROMInterface eeprom;
    const int loaded = eeprom.readWiFiSetting(ssid, sizeof(ssid), password, sizeof(password));
    int result = -1;
    if (loaded == 0 && ssid[0] != 0) {
      EMW10xxInterface *wifi = static_cast<EMW10xxInterface *>(network);
      result = wifi->set_interface(Station);
      if (result == 0) {
        result = wifi->connect(ssid, password, password[0] == 0 ? NSAPI_SECURITY_NONE : NSAPI_SECURITY_WPA_WPA2, 0);
      }
    }
    SensitiveMemory::clear(password, sizeof(password));
    SensitiveMemory::clear(ssid, sizeof(ssid));
    if (loaded != 0) {
      return fail("read saved Wi-Fi credentials", loaded);
    }
    if (result != 0 || network->get_ip_address() == nullptr || strcmp(network->get_ip_address(), "0.0.0.0") == 0) {
      return fail("saved Wi-Fi association/DHCP", result);
    }
    Serial.println(F("PASS: saved Wi-Fi association and IPv4 address. Network mode bypasses the SDK's automatic cloud telemetry."));
    return true;
  }
}

namespace NetworkTests {
  void run() {
    if (!AppConfig::kNetworkServicesEnabled) {
      return;
    }
    Screen.print(0, "Network services");
    Screen.print(1, "Saved Wi-Fi...");
    Screen.print(2, "No Azure account");
    Screen.print(3, "A:run B:help");
    Serial.print(F("Network test endpoint: "));
    Serial.println(Config::kHost);
    NetworkInterface *network = nullptr;
    if (!connectSaved(network)) {
      return;
    }
    SocketAddress address;
    const int resolved = network->gethostbyname(Config::kHost, &address, NSAPI_IPv4);
    if (resolved != 0 || address.get_ip_address() == nullptr) {
      fail("IPv4 DNS resolution", resolved);
      return;
    }
    Serial.print(F("PASS: DNS resolved test endpoint to "));
    Serial.println(address.get_ip_address());
    if (!plainHttp(network, address)) {
      return;
    }
    Screen.print(1, "NTP time sync");
    SyncTime();
    const time_t utc = time(nullptr);
    const struct tm *calendar = gmtime(&utc);
    if (IsTimeSynced() != 0 || calendar == nullptr || calendar->tm_year + 1900 < 2020) {
      fail("NTP failed; refusing TLS without a current clock", -1);
      return;
    }
    mbedtls_x509_time now = {calendar->tm_year + 1900, calendar->tm_mon + 1, calendar->tm_mday, calendar->tm_hour, calendar->tm_min, calendar->tm_sec};
    Serial.println(F("PASS: NTP supplied UTC for certificate validity checks (ordinary, unauthenticated NTP)."));
    if (https(network, address, now)) {
      Screen.print(1, "Network PASS");
      Screen.print(2, "DNS HTTP TLS OK");
      Serial.println(F("PASS: DNS, TCP, HTTP, NTP and certificate-validated HTTPS. No IoT Hub/DPS/MQTT account, keys or telemetry required."));
    }
  }
}
