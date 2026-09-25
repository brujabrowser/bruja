#include "ces/harness.h"

#include "ces/deputy.h"

namespace ces {
namespace {

void JsonEscape(std::string* o, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '"':
        *o += "\\\"";
        break;
      case '\\':
        *o += "\\\\";
        break;
      default:
        *o += c;
        break;
    }
  }
}

}  // namespace

Harness::Harness() = default;

Shot Harness::Prepare(std::string_view url) const { return Loader::Resolve(url); }

Shot Harness::PrepareWebdata() const { return Loader::ResolveWebdata(); }

Shot Harness::PrepareDino() const { return Loader::ResolveDino(); }

Shot Harness::PrepareCode(int net_error) const {
  return Loader::ResolveCode(net_error);
}

Shot Harness::PrepareInterstitial(std::string_view id) const {
  return Loader::ResolveInterstitial(id);
}

Shot Harness::PrepareDeputy(std::string_view id) const {
  return Loader::ResolveDeputy(id);
}

Shot Harness::PrepareName(std::string_view err_name) const {
  auto info = Catalog::Get().FindName(err_name);
  if (!info) {
    Shot s;
    s.ok = false;
    s.note = "unknown ERR_* name";
    return s;
  }
  return Loader::ResolveCode(info->code);
}

CesResult Harness::Send(const Shot& s) {
  last_ = s;
  history_.push_back(s);
  if (!s.ok) {
    return CES_RESULT_INVALID_ARGUMENT;
  }
  if (!fire_) {
    return CES_RESULT_OK;
  }
  return fire_(s);
}

CesResult Harness::Fire(std::string_view url) { return Send(Prepare(url)); }

CesResult Harness::FireWebdata() { return Send(PrepareWebdata()); }

CesResult Harness::FireDino() { return Send(PrepareDino()); }

CesResult Harness::FireCode(int net_error) {
  return Send(PrepareCode(net_error));
}

CesResult Harness::FireName(std::string_view err_name) {
  return Send(PrepareName(err_name));
}

CesResult Harness::FireInterstitial(std::string_view id) {
  return Send(PrepareInterstitial(id));
}

CesResult Harness::FireDeputy(std::string_view id) {
  return Send(PrepareDeputy(id));
}

std::vector<Shot> Harness::FireDownstream(Downstream d) {
  std::vector<Shot> out;
  for (const auto& dep : Deputies::Get().WithDownstream(d)) {
    Shot s = PrepareDeputy(dep.id);
    Send(s);
    out.push_back(s);
  }
  return out;
}

std::string Harness::DumpJson() {
  std::string o =
      "{\"committed\":\"chrome-error://chromewebdata/\",\"errors\":[";
  bool first = true;
  for (const auto& e : Catalog::Get().errors()) {
    if (e.code == kErrIoPending) {
      continue;
    }
    if (!first) {
      o += ',';
    }
    first = false;
    o += Loader::ResolveCode(e.code).ToJson();
  }
  o += "],\"interstitials\":[";
  first = true;
  for (const auto& i : Catalog::Get().interstitials()) {
    if (!first) {
      o += ',';
    }
    first = false;
    o += Loader::ResolveInterstitial(i.id).ToJson();
  }
  o += "],\"deputies\":";
  o += Deputies::Get().DumpJson();
  o += ",\"occupy\":[";
  first = true;
  for (const auto& d : Deputies::Get().WithDownstream(Downstream::kOccupy)) {
    if (!first) {
      o += ',';
    }
    first = false;
    Shot s = Loader::ResolveDeputy(d.id);
    s.deputy = d.id;
    s.name = d.id;
    o += s.ToJson();
  }
  o += "],\"drive\":[";
  first = true;
  for (const auto& d : Deputies::Get().WithDownstream(Downstream::kDrive)) {
    if (!first) {
      o += ',';
    }
    first = false;
    Shot s = Loader::ResolveDeputy(d.id);
    s.deputy = d.id;
    s.name = d.id;
    o += s.ToJson();
  }
  o += "]}";
  return o;
}

std::string Harness::LoadTimeDataJson(const Shot& shot) {
  std::string o = "{\"data_\":{";
  auto field = [&](const char* k, std::string_view v, bool comma) {
    if (comma) {
      o += ',';
    }
    o += '"';
    o += k;
    o += "\":\"";
    JsonEscape(&o, v);
    o += '"';
  };
  field("heading", shot.heading, false);
  field("summary", shot.summary, true);
  field("errorCode", shot.name, true);
  field("errorShort", shot.error_short, true);
  o += ",\"offlineContent\":";
  o += shot.shows_dino ? "true" : "false";
  o += ",\"disabledEasterEgg\":\"\"";
  o += "}}";
  return o;
}

}  // namespace ces
