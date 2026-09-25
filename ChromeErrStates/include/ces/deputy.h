#ifndef CES_DEPUTY_H_
#define CES_DEPUTY_H_

#include "ces/types.h"

#include <optional>
#include <string_view>
#include <vector>

namespace ces {

// Display-isolated scheme origins. chrome-error://chromewebdata/ is the
// shared unreachable origin (kUnreachableWebDataURL). Other isolated
// schemes have their own origin; a failed navigation on those schemes
// still collapses onto chromewebdata. Some origins can talk to it
// (embed / weak-origin) without being it.
struct Deputy {
  const char* id = "";
  const char* scheme = "";
  const char* origin = "";
  const char* url = "";
  Access access = Access::kNone;
  Downstream downstream = Downstream::kNone;
  bool failed_nav_commits = false;
  bool subframe_leaks = false;
  const char* component_id = "";
  const char* caller = "";
  const char* note = "";

  std::string ToJson() const;
};

class Deputies {
 public:
  static const Deputies& Get();

  std::optional<Deputy> FindId(std::string_view id) const;
  std::optional<Deputy> FindUrl(std::string_view url) const;
  const std::vector<Deputy>& all() const { return all_; }
  std::vector<Deputy> WithDownstream(Downstream d) const;
  std::string DumpJson() const;

 private:
  Deputies();
  std::vector<Deputy> all_;
};

Shot ShotFromDeputy(const Deputy& d, std::string_view trigger);

}  // namespace ces

#endif  // CES_DEPUTY_H_
