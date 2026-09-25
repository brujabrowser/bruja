#ifndef CES_TYPES_H_
#define CES_TYPES_H_

#include "ces/c/types.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ces {

// Chromium kUnreachableWebDataURL. Failed neterror navigations commit here.
inline constexpr std::string_view kChromewebdataURL =
    "chrome-error://chromewebdata/";
inline constexpr std::string_view kDinoURL = "chrome://dino/";
inline constexpr std::string_view kNetworkErrorHost = "network-error";
inline constexpr std::string_view kDinoHost = "dino";
inline constexpr std::string_view kNetworkErrorsHost = "network-errors";
inline constexpr std::string_view kInterstitialsHost = "interstitials";

// Unique URLLoader: chrome://dino is an alias for chrome://network-error/-106.
inline constexpr int kErrInternetDisconnected = -106;
inline constexpr int kErrNameNotResolved = -105;
inline constexpr int kErrIoPending = -1;
inline constexpr int kErrInvalidUrl = -300;

enum class Machine : uint32_t {
  kInvalid = CES_MACHINE_INVALID,
  kNeterror = CES_MACHINE_NETERROR,
  kSslInterstitial = CES_MACHINE_SSL_INTERSTITIAL,
  kCaptivePortal = CES_MACHINE_CAPTIVE_PORTAL,
  kSafeBrowsing = CES_MACHINE_SAFE_BROWSING,
  kHttpsOnly = CES_MACHINE_HTTPS_ONLY,
  kInsecureForm = CES_MACHINE_INSECURE_FORM,
  kEnterprise = CES_MACHINE_ENTERPRISE,
};

enum class Family : uint32_t {
  kUnknown = CES_FAMILY_UNKNOWN,
  kSystem = CES_FAMILY_SYSTEM,
  kConnection = CES_FAMILY_CONNECTION,
  kCertificate = CES_FAMILY_CERTIFICATE,
  kHttp = CES_FAMILY_HTTP,
  kCache = CES_FAMILY_CACHE,
  kOther = CES_FAMILY_OTHER,
  kDns = CES_FAMILY_DNS,
  kBlob = CES_FAMILY_BLOB,
};

enum class Access : uint32_t {
  kNone = CES_ACCESS_NONE,
  kCommit = CES_ACCESS_COMMIT,
  kIs = CES_ACCESS_IS,
  kEmbed = CES_ACCESS_EMBED,
};

enum class Downstream : uint32_t {
  kNone = CES_DOWNSTREAM_NONE,
  kOccupy = CES_DOWNSTREAM_OCCUPY,
  kDrive = CES_DOWNSTREAM_DRIVE,
};

// One OnComplete of StartNetworkErrorsURLLoader (or an isolated-scheme deputy).
struct Shot {
  bool ok = false;
  std::string trigger;
  std::string host;
  std::string scheme;
  int net_error = 0;
  std::string name;
  std::string error_short;
  std::string committed;
  Machine machine = Machine::kInvalid;
  Family family = Family::kUnknown;
  Access access = Access::kNone;
  std::string deputy;
  std::string heading;
  std::string summary;
  bool shows_dino = false;
  bool offline_class = false;
  bool chromewebdata = false;
  bool failed_nav_commits = false;
  bool subframe_leaks = false;
  Downstream downstream = Downstream::kNone;
  std::string component_id;
  std::string caller;
  std::string path;
  std::string note;

  std::string ToJson() const;
};

const char* MachineName(Machine m);
const char* FamilyName(Family f);
const char* AccessName(Access a);
const char* DownstreamName(Downstream d);
Family FamilyOf(int net_error);

}  // namespace ces

#endif  // CES_TYPES_H_
