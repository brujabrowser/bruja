#ifndef CES_HARNESS_H_
#define CES_HARNESS_H_

#include "ces/catalog.h"
#include "ces/loader.h"
#include "ces/types.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ces {

// One Fire of a Chrome error / interstitial state.
using FireFn = std::function<CesResult(const Shot&)>;

class Harness {
 public:
  Harness();

  const Catalog& catalog() const { return Catalog::Get(); }

  void set_fire(FireFn fn) { fire_ = std::move(fn); }

  Shot Prepare(std::string_view url) const;
  Shot PrepareWebdata() const;
  Shot PrepareDino() const;
  Shot PrepareCode(int net_error) const;
  Shot PrepareInterstitial(std::string_view id) const;
  Shot PrepareName(std::string_view err_name) const;
  Shot PrepareDeputy(std::string_view id) const;

  CesResult Fire(std::string_view url);
  CesResult FireWebdata();
  CesResult FireDino();
  CesResult FireCode(int net_error);
  CesResult FireName(std::string_view err_name);
  CesResult FireInterstitial(std::string_view id);
  CesResult FireDeputy(std::string_view id);
  std::vector<Shot> FireDownstream(Downstream d);

  const Shot& last() const { return last_; }
  const std::vector<Shot>& history() const { return history_; }

  // loadTimeData blob the portal seeds when it cannot navigate chrome://.
  static std::string LoadTimeDataJson(const Shot& shot);
  static std::string DumpJson();

 private:
  CesResult Send(const Shot& s);

  FireFn fire_;
  Shot last_;
  std::vector<Shot> history_;
};

}  // namespace ces

#endif  // CES_HARNESS_H_
