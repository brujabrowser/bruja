#ifndef CES_CATALOG_H_
#define CES_CATALOG_H_

#include "ces/types.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ces {

struct ErrorInfo {
  int code = 0;
  const char* name = "";
  const char* heading = "";
  const char* summary = "";
  const char* error_short = "";
  bool shows_dino = false;
};

struct InterstitialInfo {
  const char* id = "";
  const char* url = "";
  Machine machine = Machine::kInvalid;
  const char* heading = "";
  const char* note = "";
};

class Catalog {
 public:
  static const Catalog& Get();

  std::optional<ErrorInfo> FindCode(int code) const;
  std::optional<ErrorInfo> FindName(std::string_view name) const;
  std::optional<InterstitialInfo> FindInterstitial(std::string_view id) const;

  // True when StartNetworkErrorsURLLoader would accept the code
  // (IsValidNetworkErrorCode && code != ERR_IO_PENDING).
  bool IsValidNetworkErrorCode(int code) const;

  const std::vector<ErrorInfo>& errors() const { return errors_; }
  const std::vector<InterstitialInfo>& interstitials() const {
    return interstitials_;
  }
  std::size_t size() const { return errors_.size(); }

 private:
  Catalog();
  std::vector<ErrorInfo> errors_;
  std::vector<InterstitialInfo> interstitials_;
};

}  // namespace ces

#endif  // CES_CATALOG_H_
