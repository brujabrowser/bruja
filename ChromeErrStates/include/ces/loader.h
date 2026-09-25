#ifndef CES_LOADER_H_
#define CES_LOADER_H_

#include "ces/types.h"

#include <string_view>

namespace ces {

// Pure model of content/browser/webui/network_error_url_loader.cc
// StartNetworkErrorsURLLoader. Chromium sources are spec only.
//
// Canonical chromewebdata occupy:
//   chrome://network-error/<N> → URLLoader OnComplete(N)
// chrome://dino is an optional alias for N = -106. Neither is required
// beyond any valid network-error code (or any failed navigation).
class Loader {
 public:
  static Shot Resolve(std::string_view url);
  static Shot ResolveWebdata();  // chrome://network-error/-106
  static Shot ResolveDino();     // alias of ResolveWebdata
  static Shot ResolveCode(int net_error);
  static Shot ResolveInterstitial(std::string_view id);
  static Shot ResolveDeputy(std::string_view id);

  // Host of a chrome:// URL, lowercase, no port. Empty if not chrome:.
  static std::string HostOf(std::string_view url);
  static std::string PathOf(std::string_view url);
};

}  // namespace ces

#endif  // CES_LOADER_H_
