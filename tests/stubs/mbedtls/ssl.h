#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr int MBEDTLS_ERR_SSL_WANT_READ = -1001;
constexpr int MBEDTLS_ERR_SSL_WANT_WRITE = -1002;
constexpr uint32_t MBEDTLS_X509_BADCERT_FUTURE = 1;
constexpr uint32_t MBEDTLS_X509_BADCERT_EXPIRED = 2;
constexpr uint32_t MBEDTLS_X509_BADCERT_CN_MISMATCH = 4;
constexpr int MBEDTLS_SSL_IS_CLIENT = 0;
constexpr int MBEDTLS_SSL_TRANSPORT_STREAM = 0;
constexpr int MBEDTLS_SSL_PRESET_DEFAULT = 0;
constexpr int MBEDTLS_SSL_VERIFY_REQUIRED = 2;
constexpr int MBEDTLS_SSL_MAJOR_VERSION_3 = 3;
constexpr int MBEDTLS_SSL_MINOR_VERSION_3 = 3;

struct mbedtls_x509_time {
  int year, mon, day, hour, min, sec;
};

struct mbedtls_x509_crt {
  mbedtls_x509_time valid_from = {2015, 1, 1, 0, 0, 0};
  mbedtls_x509_time valid_to = {2035, 1, 1, 0, 0, 0};
};
struct mbedtls_entropy_context {};
struct mbedtls_ctr_drbg_context {};
typedef int (*TestVerifyCallback)(void *, mbedtls_x509_crt *, int, uint32_t *);

struct mbedtls_ssl_config {
  int authmode = 0;
  int minorVersion = 0;
  mbedtls_x509_crt *root = nullptr;
  TestVerifyCallback verify = nullptr;
  void *verifyContext = nullptr;
};

struct mbedtls_ssl_context {
  const mbedtls_ssl_config *config = nullptr;
  const char *host = nullptr;
  void *socket = nullptr;
  int (*send)(void *, const unsigned char *, size_t) = nullptr;
  int (*receive)(void *, unsigned char *, size_t) = nullptr;
  uint32_t flags = 0;
};

void mbedtls_entropy_init(mbedtls_entropy_context *);
void mbedtls_entropy_free(mbedtls_entropy_context *);
int mbedtls_entropy_func(void *, unsigned char *, size_t);
void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *);
void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *);
int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *, int (*)(void *, unsigned char *, size_t), void *, const unsigned char *, size_t);
int mbedtls_ctr_drbg_random(void *, unsigned char *, size_t);
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *, const unsigned char *, size_t);
int mbedtls_x509_crt_verify(mbedtls_x509_crt *, mbedtls_x509_crt *, void *, const char *, uint32_t *, TestVerifyCallback, void *);
void mbedtls_ssl_config_init(mbedtls_ssl_config *);
void mbedtls_ssl_config_free(mbedtls_ssl_config *);
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *, int, int, int);
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *, int);
void mbedtls_ssl_conf_min_version(mbedtls_ssl_config *, int, int);
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *, mbedtls_x509_crt *, void *);
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *, int (*)(void *, unsigned char *, size_t), void *);
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *, TestVerifyCallback, void *);
void mbedtls_ssl_init(mbedtls_ssl_context *);
void mbedtls_ssl_free(mbedtls_ssl_context *);
int mbedtls_ssl_setup(mbedtls_ssl_context *, const mbedtls_ssl_config *);
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *, const char *);
void mbedtls_ssl_set_bio(mbedtls_ssl_context *, void *, int (*)(void *, const unsigned char *, size_t), int (*)(void *, unsigned char *, size_t), void *);
int mbedtls_ssl_handshake(mbedtls_ssl_context *);
uint32_t mbedtls_ssl_get_verify_result(mbedtls_ssl_context *);
const mbedtls_x509_crt *mbedtls_ssl_get_peer_cert(mbedtls_ssl_context *);
int mbedtls_ssl_write(mbedtls_ssl_context *, const unsigned char *, size_t);
int mbedtls_ssl_read(mbedtls_ssl_context *, unsigned char *, size_t);
