#ifndef CES_C_TYPES_H_
#define CES_C_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CesResult;

#define CES_RESULT_OK 0u
#define CES_RESULT_CANCELLED 1u
#define CES_RESULT_UNKNOWN 2u
#define CES_RESULT_INVALID_ARGUMENT 3u
#define CES_RESULT_NOT_FOUND 5u
#define CES_RESULT_RESOURCE_EXHAUSTED 8u

#define CES_MACHINE_INVALID 0u
#define CES_MACHINE_NETERROR 1u
#define CES_MACHINE_SSL_INTERSTITIAL 2u
#define CES_MACHINE_CAPTIVE_PORTAL 3u
#define CES_MACHINE_SAFE_BROWSING 4u
#define CES_MACHINE_HTTPS_ONLY 5u
#define CES_MACHINE_INSECURE_FORM 6u
#define CES_MACHINE_ENTERPRISE 7u

#define CES_FAMILY_UNKNOWN 0u
#define CES_FAMILY_SYSTEM 1u
#define CES_FAMILY_CONNECTION 2u
#define CES_FAMILY_CERTIFICATE 3u
#define CES_FAMILY_HTTP 4u
#define CES_FAMILY_CACHE 5u
#define CES_FAMILY_OTHER 6u
#define CES_FAMILY_DNS 7u
#define CES_FAMILY_BLOB 8u

// How this origin relates to chrome-error://chromewebdata/.
#define CES_ACCESS_NONE 0u
#define CES_ACCESS_COMMIT 1u
#define CES_ACCESS_IS 2u
#define CES_ACCESS_EMBED 3u

// What a downstream document can do with this deputy.
#define CES_DOWNSTREAM_NONE 0u
#define CES_DOWNSTREAM_OCCUPY 1u
#define CES_DOWNSTREAM_DRIVE 2u

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CES_C_TYPES_H_
