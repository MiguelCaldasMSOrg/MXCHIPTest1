#include <Arduino.h>
#include <mbedtls/aes.h>
#include <mbedtls/cipher.h>
#include <mbedtls/cmac.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include "Az3166StSafe.h"

namespace {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context rng;
  bool randomReady = false;
  mbedtls_cipher_context_t cmac;
  bool cmacActive = false;
  uint8_t cmacLength = 0;

  const mbedtls_cipher_info_t *cipherFor(size_t keySize) {
    if (keySize == 16) {
      return mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    }
    if (keySize == 32) {
      return mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_ECB);
    }
    return nullptr;
  }

  void freeCmac() {
    if (cmacActive) {
      mbedtls_cipher_free(&cmac);
    }
    cmacActive = false;
    cmacLength = 0;
  }

  bool sameTag(const uint8_t *first, const uint8_t *second, size_t size) {
    volatile uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
      difference |= first[index] ^ second[index];
    }
    return difference == 0;
  }

  stse_ReturnCode_t cbc(bool encrypt, const uint8_t *input, uint16_t size, const uint8_t *iv, const uint8_t *key, uint16_t keySize, uint8_t *output, uint16_t *outputSize) {
    if (input == nullptr || iv == nullptr || key == nullptr || output == nullptr || outputSize == nullptr || size == 0 || size % 16 != 0 || cipherFor(keySize) == nullptr) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    uint8_t initial[16];
    memcpy(initial, iv, sizeof(initial));
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    int result = encrypt ? mbedtls_aes_setkey_enc(&context, key, keySize * 8) : mbedtls_aes_setkey_dec(&context, key, keySize * 8);
    if (result == 0) {
      result = mbedtls_aes_crypt_cbc(&context, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, size, initial, input, output);
    }
    mbedtls_aes_free(&context);
    Az3166StSafe::clear(initial, sizeof(initial));
    *outputSize = result == 0 ? size : 0;
    return result == 0 ? STSE_OK : (encrypt ? STSE_PLATFORM_AES_CBC_ENCRYPT_ERROR : STSE_PLATFORM_AES_CBC_DECRYPT_ERROR);
  }
}

namespace Az3166StSafe {
  stse_ReturnCode_t cryptoSelfTest() {
    // Public NIST SP 800-38A and RFC 4493/5869 known-answer inputs, never device keys.
    const uint8_t key128[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
    uint8_t plaintext[] = {0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96, 0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A};
    uint8_t iv[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8_t ecb128[] = {0x3A, 0xD7, 0x7B, 0xB4, 0x0D, 0x7A, 0x36, 0x60, 0xA8, 0x9E, 0xCA, 0xF3, 0x24, 0x66, 0xEF, 0x97};
    const uint8_t cbc128[] = {0x76, 0x49, 0xAB, 0xAC, 0x81, 0x19, 0xB2, 0x46, 0xCE, 0xE9, 0x8E, 0x9B, 0x12, 0xE9, 0x19, 0x7D};
    const uint8_t mac128[] = {0x07, 0x0A, 0x16, 0xB4, 0x6B, 0x4D, 0x41, 0x44, 0xF7, 0x9B, 0xDD, 0x9D, 0xD0, 0x4A, 0x28, 0x7C};
    const uint8_t emptyMac[] = {0xBB, 0x1D, 0x69, 0x29, 0xE9, 0x59, 0x37, 0x28, 0x7F, 0xA3, 0x7D, 0x12, 0x9B, 0x75, 0x67, 0x46};
    uint8_t output[64] = {};
    uint16_t size = sizeof(output);
    if (stse_platform_aes_ecb_enc(plaintext, 16, key128, 16, output, &size) != STSE_OK || size != 16 || memcmp(output, ecb128, 16) != 0) {
      return STSE_PLATFORM_AES_ECB_ENCRYPT_ERROR;
    }
    if (stse_platform_aes_cbc_enc(plaintext, 16, iv, key128, 16, output, &size) != STSE_OK || size != 16 || memcmp(output, cbc128, 16) != 0) {
      return STSE_PLATFORM_AES_CBC_ENCRYPT_ERROR;
    }
    if (stse_platform_aes_cbc_dec(output, 16, iv, key128, 16, output, &size) != STSE_OK || size != 16 || memcmp(output, plaintext, 16) != 0) {
      return STSE_PLATFORM_AES_CBC_DECRYPT_ERROR;
    }
    const uint8_t key256[] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81, 0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7, 0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4};
    const uint8_t ecb256[] = {0xF3, 0xEE, 0xD1, 0xBD, 0xB5, 0xD2, 0xA0, 0x3C, 0x06, 0x4B, 0x5A, 0x7E, 0x3D, 0xB1, 0x81, 0xF8};
    const uint8_t cbc256[] = {0xF5, 0x8C, 0x4C, 0x04, 0xD6, 0xE5, 0xF1, 0xBA, 0x77, 0x9E, 0xAB, 0xFB, 0x5F, 0x7B, 0xFB, 0xD6};
    if (stse_platform_aes_ecb_enc(plaintext, 16, key256, 32, output, &size) != STSE_OK || memcmp(output, ecb256, 16) != 0) {
      return STSE_PLATFORM_AES_ECB_ENCRYPT_ERROR;
    }
    if (stse_platform_aes_cbc_enc(plaintext, 16, iv, key256, 32, output, &size) != STSE_OK || memcmp(output, cbc256, 16) != 0 || stse_platform_aes_cbc_dec(output, 16, iv, key256, 32, output, &size) != STSE_OK || memcmp(output, plaintext, 16) != 0) {
      return STSE_PLATFORM_AES_CBC_DECRYPT_ERROR;
    }
    if (stse_platform_aes_cmac_compute(plaintext, 16, key128, 16, 16, output, &size) != STSE_OK || size != 16 || memcmp(output, mac128, 16) != 0) {
      Serial.println(F("STSE host self-test failed: RFC 4493 one-block CMAC."));
      return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
    }
    if (stse_platform_aes_cmac_compute(nullptr, 0, key128, 16, 16, output, &size) != STSE_OK || memcmp(output, emptyMac, 16) != 0) {
      Serial.println(F("STSE host self-test failed: RFC 4493 empty-message CMAC."));
      return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
    }
    uint8_t smallSize = 0;
    if (stse_platform_aes_cmac_init(key128, 16, 8) != STSE_OK || stse_platform_aes_cmac_append(plaintext, 3) != STSE_OK || stse_platform_aes_cmac_append(plaintext + 3, 13) != STSE_OK || stse_platform_aes_cmac_compute_finish(output, &smallSize) != STSE_OK || smallSize != 8 ||
      memcmp(output, mac128, 8) != 0) {
      Serial.println(F("STSE host self-test failed: fragmented/truncated CMAC."));
      return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
    }
    if (stse_platform_aes_cmac_verify(plaintext, 16, key128, 16, mac128, 8) != STSE_OK) {
      return STSE_PLATFORM_AES_CMAC_VERIFY_ERROR;
    }
    output[0] ^= 1;
    if (stse_platform_aes_cmac_verify(plaintext, 16, key128, 16, output, 8) != STSE_PLATFORM_AES_CMAC_VERIFY_ERROR || stse_platform_aes_cbc_enc(plaintext, 15, iv, key128, 16, output, &size) != STSE_PLATFORM_INVALID_PARAMETER ||
      stse_platform_aes_ecb_enc(plaintext, 16, key128, 15, output, &size) != STSE_PLATFORM_INVALID_PARAMETER) {
      return STSE_PLATFORM_AES_CMAC_VERIFY_ERROR;
    }
    uint8_t ikm[22];
    memset(ikm, 0x0B, sizeof(ikm));
    uint8_t salt[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t info[] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9};
    const uint8_t hkdf[] = {0x3C, 0xB2, 0x5F, 0x25, 0xFA, 0xAC, 0xD5, 0x7A, 0x90, 0x43, 0x4F, 0x64, 0xD0, 0x36, 0x2F, 0x2A, 0x2D, 0x2D, 0x0A, 0x90, 0xCF, 0x1A, 0x5A, 0x4C, 0x5D, 0xB0, 0x2D, 0x56, 0xEC, 0xC4, 0xC5, 0xBF, 0x34, 0x00, 0x72, 0x08, 0xD5, 0xB8, 0x87, 0x18, 0x58, 0x65};
    if (stse_platform_hmac_sha256_compute(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), output, sizeof(hkdf)) != STSE_OK || memcmp(output, hkdf, sizeof(hkdf)) != 0) {
      return STSE_PLATFORM_HKDF_ERROR;
    }
    clear(output, sizeof(output));
    return STSE_OK;
  }
}

extern "C" {
  stse_ReturnCode_t stse_platform_crypto_init() {
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_generate_random_init() {
    if (randomReady) {
      return STSE_OK;
    }
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    const unsigned char label[] = "AZ3166-STSELib";
    if (mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, label, sizeof(label) - 1) != 0) {
      mbedtls_ctr_drbg_free(&rng);
      mbedtls_entropy_free(&entropy);
      return STSE_PLATFORM_CRYPTO_INIT_ERROR;
    }
    randomReady = true;
    return STSE_OK;
  }

  uint32_t stse_platform_generate_random() {
    uint32_t value = 0;
    if (!randomReady || mbedtls_ctr_drbg_random(&rng, reinterpret_cast<uint8_t *>(&value), sizeof(value)) != 0) {
      // The upstream callback has no error return; never substitute predictable random data.
      error("STSELib host RNG failed; refusing to continue.");
    }
    return value;
  }

  stse_ReturnCode_t stse_platform_hash_compute(stse_hash_algorithm_t algorithm, uint8_t *data, uint16_t size, uint8_t *output, uint16_t *outputSize) {
    if ((data == nullptr && size != 0) || output == nullptr || outputSize == nullptr) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    mbedtls_md_type_t type;
    switch (algorithm) {
      case STSE_SHA_224:
        type = MBEDTLS_MD_SHA224;
        break;
      case STSE_SHA_256:
        type = MBEDTLS_MD_SHA256;
        break;
      case STSE_SHA_384:
        type = MBEDTLS_MD_SHA384;
        break;
      case STSE_SHA_512:
        type = MBEDTLS_MD_SHA512;
        break;
      default:
        return STSE_PLATFORM_INVALID_PARAMETER;
    }
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
    const uint8_t empty = 0;
    if (info == nullptr || mbedtls_md(info, data == nullptr ? &empty : data, size, output) != 0) {
      *outputSize = 0;
      return STSE_PLATFORM_HASH_ERROR;
    }
    *outputSize = mbedtls_md_get_size(info);
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_ecc_verify(stse_ecc_key_type_t type, const uint8_t *key, uint8_t *digest, uint16_t digestSize, uint8_t *signature) {
    if (key == nullptr || digest == nullptr || signature == nullptr || digestSize == 0 || (type != STSE_ECC_KT_NIST_P_256 && type != STSE_ECC_KT_NIST_P_384)) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    const mbedtls_ecp_group_id groupId = type == STSE_ECC_KT_NIST_P_256 ? MBEDTLS_ECP_DP_SECP256R1 : MBEDTLS_ECP_DP_SECP384R1;
    const size_t coordinateSize = type == STSE_ECC_KT_NIST_P_256 ? 32 : 48;
    uint8_t point[97];
    point[0] = 4;
    memcpy(point + 1, key, coordinateSize * 2);
    mbedtls_ecdsa_context context;
    mbedtls_ecdsa_init(&context);
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int result = mbedtls_ecp_group_load(&context.grp, groupId);
    if (result == 0) {
      result = mbedtls_ecp_point_read_binary(&context.grp, &context.Q, point, 1 + coordinateSize * 2);
    }
    if (result == 0) {
      result = mbedtls_ecp_check_pubkey(&context.grp, &context.Q);
    }
    if (result == 0) {
      result = mbedtls_mpi_read_binary(&r, signature, coordinateSize);
    }
    if (result == 0) {
      result = mbedtls_mpi_read_binary(&s, signature + coordinateSize, coordinateSize);
    }
    if (result == 0) {
      result = mbedtls_ecdsa_verify(&context.grp, digest, digestSize, &context.Q, &r, &s);
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecdsa_free(&context);
    return result == 0 ? STSE_OK : STSE_PLATFORM_ECC_VERIFY_ERROR;
  }

  stse_ReturnCode_t stse_platform_aes_cmac_init(const uint8_t *key, uint16_t keySize, uint16_t tagSize) {
    freeCmac();
    const mbedtls_cipher_info_t *info = cipherFor(keySize);
    if (key == nullptr || info == nullptr || tagSize == 0 || tagSize > 16) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    mbedtls_cipher_init(&cmac);
    cmacActive = true;
    if (mbedtls_cipher_setup(&cmac, info) != 0 || mbedtls_cipher_cmac_starts(&cmac, key, keySize * 8) != 0) {
      freeCmac();
      return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
    }
    cmacLength = static_cast<uint8_t>(tagSize);
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_aes_cmac_append(uint8_t *data, uint16_t size) {
    if (!cmacActive || (data == nullptr && size != 0)) {
      freeCmac();
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    if (size != 0 && mbedtls_cipher_cmac_update(&cmac, data, size) != 0) {
      freeCmac();
      return STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
    }
    return STSE_OK;
  }

  stse_ReturnCode_t stse_platform_aes_cmac_compute_finish(uint8_t *tag, uint8_t *tagSize) {
    if (!cmacActive || tag == nullptr || tagSize == nullptr) {
      freeCmac();
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    uint8_t complete[16];
    const int result = mbedtls_cipher_cmac_finish(&cmac, complete);
    *tagSize = result == 0 ? cmacLength : 0;
    if (result == 0) {
      memcpy(tag, complete, cmacLength);
    }
    Az3166StSafe::clear(complete, sizeof(complete));
    freeCmac();
    return result == 0 ? STSE_OK : STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
  }

  stse_ReturnCode_t stse_platform_aes_cmac_verify_finish(uint8_t *tag) {
    if (tag == nullptr) {
      freeCmac();
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    uint8_t computed[16];
    uint8_t size = 0;
    const stse_ReturnCode_t result = stse_platform_aes_cmac_compute_finish(computed, &size);
    const bool valid = result == STSE_OK && sameTag(computed, tag, size);
    Az3166StSafe::clear(computed, sizeof(computed));
    return result != STSE_OK ? result : (valid ? STSE_OK : STSE_PLATFORM_AES_CMAC_VERIFY_ERROR);
  }

  stse_ReturnCode_t stse_platform_aes_cmac_compute(const uint8_t *data, uint16_t size, const uint8_t *key, uint16_t keySize, uint16_t tagSize, uint8_t *tag, uint16_t *actualSize) {
    if ((data == nullptr && size != 0) || key == nullptr || tag == nullptr || actualSize == nullptr || tagSize == 0 || tagSize > 16 || cipherFor(keySize) == nullptr) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    uint8_t computed[16];
    // This mbedTLS build requires a non-null pointer even for an empty message.
    const uint8_t empty = 0;
    const int result = mbedtls_cipher_cmac(cipherFor(keySize), key, keySize * 8, data == nullptr ? &empty : data, size, computed);
    *actualSize = result == 0 ? tagSize : 0;
    if (result == 0) {
      memcpy(tag, computed, tagSize);
    }
    Az3166StSafe::clear(computed, sizeof(computed));
    return result == 0 ? STSE_OK : STSE_PLATFORM_AES_CMAC_COMPUTE_ERROR;
  }

  stse_ReturnCode_t stse_platform_aes_cmac_verify(const uint8_t *data, uint16_t size, const uint8_t *key, uint16_t keySize, const uint8_t *tag, uint16_t tagSize) {
    if (tag == nullptr) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    uint8_t computed[16];
    uint16_t actual = 0;
    const stse_ReturnCode_t result = stse_platform_aes_cmac_compute(data, size, key, keySize, tagSize, computed, &actual);
    const bool valid = result == STSE_OK && sameTag(computed, tag, actual);
    Az3166StSafe::clear(computed, sizeof(computed));
    return result != STSE_OK ? result : (valid ? STSE_OK : STSE_PLATFORM_AES_CMAC_VERIFY_ERROR);
  }

  stse_ReturnCode_t stse_platform_aes_cbc_enc(const uint8_t *input, uint16_t size, uint8_t *iv, const uint8_t *key, uint16_t keySize, uint8_t *output, uint16_t *outputSize) {
    return cbc(true, input, size, iv, key, keySize, output, outputSize);
  }

  stse_ReturnCode_t stse_platform_aes_cbc_dec(const uint8_t *input, uint16_t size, uint8_t *iv, const uint8_t *key, uint16_t keySize, uint8_t *output, uint16_t *outputSize) {
    return cbc(false, input, size, iv, key, keySize, output, outputSize);
  }

  stse_ReturnCode_t stse_platform_aes_ecb_enc(const uint8_t *input, uint16_t size, const uint8_t *key, uint16_t keySize, uint8_t *output, uint16_t *outputSize) {
    if (input == nullptr || key == nullptr || output == nullptr || outputSize == nullptr || size == 0 || size % 16 != 0 || cipherFor(keySize) == nullptr) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    int result = mbedtls_aes_setkey_enc(&context, key, keySize * 8);
    for (size_t index = 0; result == 0 && index < size; index += 16) {
      result = mbedtls_aes_crypt_ecb(&context, MBEDTLS_AES_ENCRYPT, input + index, output + index);
    }
    mbedtls_aes_free(&context);
    *outputSize = result == 0 ? size : 0;
    return result == 0 ? STSE_OK : STSE_PLATFORM_AES_ECB_ENCRYPT_ERROR;
  }

  stse_ReturnCode_t stse_platform_hmac_sha256_extract(uint8_t *salt, uint16_t saltSize, uint8_t *input, uint16_t inputSize, uint8_t *output, uint16_t outputSize) {
    if (output == nullptr || outputSize != 32 || (input == nullptr && inputSize != 0) || (salt == nullptr && saltSize != 0)) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    return mbedtls_hkdf_extract(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, saltSize, input, inputSize, output) == 0 ? STSE_OK : STSE_PLATFORM_HKDF_ERROR;
  }

  stse_ReturnCode_t stse_platform_hmac_sha256_expand(uint8_t *key, uint16_t keySize, uint8_t *info, uint16_t infoSize, uint8_t *output, uint16_t outputSize) {
    if (key == nullptr || keySize < 32 || output == nullptr || outputSize == 0 || (info == nullptr && infoSize != 0)) {
      return STSE_PLATFORM_INVALID_PARAMETER;
    }
    return mbedtls_hkdf_expand(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keySize, info, infoSize, output, outputSize) == 0 ? STSE_OK : STSE_PLATFORM_HKDF_ERROR;
  }

  stse_ReturnCode_t stse_platform_nist_kw_encrypt(uint8_t *, uint32_t, uint8_t *, uint8_t, uint8_t *, uint32_t *outputSize) {
    if (outputSize != nullptr) {
      *outputSize = 0;
    }
    Serial.println(F("STSELib: host NIST key-wrap is unavailable in the AZ3166 mbedTLS build; wrapped provisioning is not enabled."));
    return STSE_PLATFORM_KEYWRAP_ERROR;
  }
}
