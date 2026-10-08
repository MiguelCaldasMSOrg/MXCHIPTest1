#pragma once

#include "stse_platform_generic.h"

#define STSE_CONF_STSAFE_A_SUPPORT
#define STSE_CONF_ECC_NIST_P_256
#define STSE_CONF_ECC_NIST_P_384
#define STSE_CONF_HASH_SHA_224
#define STSE_CONF_HASH_SHA_256
#define STSE_CONF_HASH_SHA_384
#define STSE_CONF_HASH_SHA_512
#define STSE_CONF_USE_HOST_SESSION
// The AZ3166's A100 rejects the newer QUERY command-authorization table (tag 0x24).
#define STSE_CONF_USE_STATIC_PERSONALIZATION_INFORMATIONS

// Native MiCO logs busy-address NACKs as assertions; wait the per-command A100 timing first.
#define STSE_MAX_POLLING_RETRY 50
#define STSE_FIRST_POLLING_INTERVAL 10
#define STSE_POLLING_RETRY_INTERVAL 10
