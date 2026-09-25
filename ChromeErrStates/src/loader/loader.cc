#include "ces/loader.h"

#include "ces/catalog.h"
#include "ces/deputy.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace ces {
namespace {

std::string Lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return o;
}

void JsonEscape(std::string* o, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '"':
        *o += "\\\"";
        break;
      case '\\':
        *o += "\\\\";
        break;
      case '\n':
        *o += "\\n";
        break;
      case '\r':
        *o += "\\r";
        break;
      default:
        *o += c;
        break;
    }
  }
}

Shot FillFromError(int net_error, std::string trigger, std::string host) {
  Shot s;
  s.trigger = std::move(trigger);
  s.host = std::move(host);
  s.scheme = "chrome-error";
  s.net_error = net_error;
  s.committed = std::string(kChromewebdataURL);
  s.path = "url_loader_on_complete";
  s.machine = Machine::kNeterror;
  s.family = FamilyOf(net_error);
  s.access = Access::kCommit;
  s.deputy = "chromewebdata";
  s.offline_class = true;
  s.chromewebdata = true;
  s.failed_nav_commits = true;
  s.subframe_leaks = true;
  s.downstream = Downstream::kOccupy;
  s.caller = "StartNetworkErrorsURLLoader";

  const Catalog& cat = Catalog::Get();
  if (auto info = cat.FindCode(net_error)) {
    s.ok = true;
    s.name = info->name;
    s.error_short = info->error_short;
    s.heading = info->heading;
    s.summary = info->summary;
    s.shows_dino = info->shows_dino;
  } else {
    s.ok = true;
    s.name = "ERR_INVALID_URL";
    s.error_short = "ERR_INVALID_URL";
    s.net_error = kErrInvalidUrl;
    s.heading = "This site can't be reached";
    s.summary = "The URL is invalid.";
    s.family = FamilyOf(kErrInvalidUrl);
    s.note = "unknown or disallowed net error code; URLLoader uses ERR_INVALID_URL";
  }
  if (s.host == kDinoHost) {
    s.note =
        "chrome://dino is an alias for chrome://network-error/-106; "
        "StartNetworkErrorsURLLoader OnComplete(ERR_INTERNET_DISCONNECTED)";
  }
  return s;
}

}  // namespace

const char* MachineName(Machine m) {
  switch (m) {
    case Machine::kNeterror:
      return "neterror";
    case Machine::kSslInterstitial:
      return "ssl_interstitial";
    case Machine::kCaptivePortal:
      return "captive_portal";
    case Machine::kSafeBrowsing:
      return "safebrowsing";
    case Machine::kHttpsOnly:
      return "https_only";
    case Machine::kInsecureForm:
      return "insecure_form";
    case Machine::kEnterprise:
      return "enterprise";
    default:
      return "invalid";
  }
}

const char* AccessName(Access a) {
  switch (a) {
    case Access::kCommit:
      return "commit";
    case Access::kIs:
      return "is";
    case Access::kEmbed:
      return "embed";
    default:
      return "none";
  }
}

const char* DownstreamName(Downstream d) {
  switch (d) {
    case Downstream::kOccupy:
      return "occupy";
    case Downstream::kDrive:
      return "drive";
    default:
      return "none";
  }
}

const char* FamilyName(Family f) {
  switch (f) {
    case Family::kSystem:
      return "system";
    case Family::kConnection:
      return "connection";
    case Family::kCertificate:
      return "certificate";
    case Family::kHttp:
      return "http";
    case Family::kCache:
      return "cache";
    case Family::kOther:
      return "other";
    case Family::kDns:
      return "dns";
    case Family::kBlob:
      return "blob";
    default:
      return "unknown";
  }
}

Family FamilyOf(int net_error) {
  int n = net_error < 0 ? -net_error : net_error;
  if (n >= 1 && n <= 99) {
    return Family::kSystem;
  }
  if (n >= 100 && n <= 199) {
    return Family::kConnection;
  }
  if (n >= 200 && n <= 299) {
    return Family::kCertificate;
  }
  if (n >= 300 && n <= 399) {
    return Family::kHttp;
  }
  if (n >= 400 && n <= 499) {
    return Family::kCache;
  }
  if (n >= 800 && n <= 899) {
    return Family::kDns;
  }
  if (n >= 900 && n <= 999) {
    return Family::kBlob;
  }
  return Family::kOther;
}

std::string Shot::ToJson() const {
  std::string o = "{";
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
  auto num = [&](const char* k, int v, bool comma) {
    if (comma) {
      o += ',';
    }
    o += '"';
    o += k;
    o += "\":";
    o += std::to_string(v);
  };
  auto flag = [&](const char* k, bool v, bool comma) {
    if (comma) {
      o += ',';
    }
    o += '"';
    o += k;
    o += "\":";
    o += v ? "true" : "false";
  };
  flag("ok", ok, false);
  field("trigger", trigger, true);
  field("host", host, true);
  field("scheme", scheme, true);
  num("net_error", net_error, true);
  field("name", name, true);
  field("error_short", error_short, true);
  field("committed", committed, true);
  field("machine", MachineName(machine), true);
  field("family", FamilyName(family), true);
  field("access", AccessName(access), true);
  field("deputy", deputy, true);
  field("heading", heading, true);
  field("summary", summary, true);
  flag("shows_dino", shows_dino, true);
  flag("offline_class", offline_class, true);
  flag("chromewebdata", chromewebdata, true);
  flag("failed_nav_commits", failed_nav_commits, true);
  flag("subframe_leaks", subframe_leaks, true);
  field("downstream", DownstreamName(downstream), true);
  field("component_id", component_id, true);
  field("caller", caller, true);
  field("path", path, true);
  field("note", note, true);
  o += '}';
  return o;
}

std::string Loader::HostOf(std::string_view url) {
  std::string u = Lower(url);
  auto sep = u.find("://");
  if (sep == std::string::npos) {
    return {};
  }
  std::string rest = u.substr(sep + 3);
  auto slash = rest.find('/');
  auto q = rest.find('?');
  auto end = rest.size();
  if (slash != std::string::npos) {
    end = slash;
  }
  if (q != std::string::npos && q < end) {
    end = q;
  }
  std::string host = rest.substr(0, end);
  auto colon = host.find(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  return host;
}

std::string Loader::PathOf(std::string_view url) {
  std::string u(url);
  auto sep = u.find("://");
  if (sep == std::string::npos) {
    return {};
  }
  std::string rest = u.substr(sep + 3);
  auto slash = rest.find('/');
  if (slash == std::string::npos) {
    return "/";
  }
  std::string path = rest.substr(slash);
  auto q = path.find('?');
  if (q != std::string::npos) {
    path = path.substr(0, q);
  }
  auto hash = path.find('#');
  if (hash != std::string::npos) {
    path = path.substr(0, hash);
  }
  return path;
}

Shot Loader::ResolveDeputy(std::string_view id) {
  auto d = Deputies::Get().FindId(id);
  if (!d) {
    Shot s;
    s.ok = false;
    s.note = "unknown isolated-scheme deputy";
    return s;
  }
  if (d->access == Access::kCommit) {
    return Resolve(d->url);
  }
  return ShotFromDeputy(*d, d->url);
}

Shot Loader::ResolveWebdata() { return ResolveCode(kErrInternetDisconnected); }

Shot Loader::ResolveDino() { return Resolve(kDinoURL); }

Shot Loader::ResolveCode(int net_error) {
  std::string url = "chrome://network-error/";
  url += std::to_string(net_error);
  return Resolve(url);
}

Shot Loader::ResolveInterstitial(std::string_view id) {
  Shot s;
  s.path = "interstitial_preview";
  auto info = Catalog::Get().FindInterstitial(id);
  if (!info) {
    s.ok = false;
    s.trigger = "chrome://interstitials/";
    s.trigger += std::string(id);
    s.host = std::string(kInterstitialsHost);
    s.machine = Machine::kInvalid;
    s.note = "unknown chrome://interstitials/ id";
    return s;
  }
  s.ok = true;
  s.trigger = info->url;
  s.host = std::string(kInterstitialsHost);
  s.scheme = "chrome";
  s.committed = info->url;
  s.machine = info->machine;
  s.heading = info->heading;
  s.note = info->note;
  s.name = info->id;
  s.error_short = info->id;
  s.family = Family::kUnknown;
  s.access = Access::kNone;
  s.deputy = "interstitials";
  s.offline_class = false;
  s.shows_dino = false;
  s.chromewebdata = false;
  s.failed_nav_commits = true;
  s.subframe_leaks = false;
  return s;
}

Shot Loader::Resolve(std::string_view url) {
  std::string u(url);
  if (u.empty()) {
    Shot s;
    s.ok = false;
    s.note = "empty url";
    s.machine = Machine::kInvalid;
    return s;
  }

  std::string host = HostOf(u);
  std::string path = PathOf(u);
  std::string scheme;
  auto sep = Lower(u).find("://");
  if (sep != std::string::npos) {
    scheme = Lower(u).substr(0, sep);
  }

  if (scheme == "chrome-error" && host == "chromewebdata") {
    Shot s = FillFromError(kErrInternetDisconnected, u, host);
    s.access = Access::kIs;
    s.committed = std::string(kChromewebdataURL);
    s.note =
        "already on chrome-error://chromewebdata/; display-isolated opaque "
        "origin. Renderer cannot navigate here directly. Subframe errors "
        "commit this URL in the embedder process.";
    s.path = "committed_chromewebdata";
    return s;
  }

  if (host != kInterstitialsHost) {
    if (auto dep = Deputies::Get().FindUrl(u)) {
      if (dep->access != Access::kCommit) {
        return ShotFromDeputy(*dep, u);
      }
    }
  }

  if (scheme == "http" || scheme == "https") {
    Shot s = FillFromError(kErrInternetDisconnected, u, host);
    s.scheme = scheme;
    s.host = host;
    s.note =
        "Failed navigation of any URL commits chrome-error://chromewebdata/ "
        "(error page isolation). Same confused deputy as chrome://dino.";
    s.path = "failed_navigation";
    return s;
  }

  if (host == kInterstitialsHost) {
    std::string id = path;
    if (!id.empty() && id[0] == '/') {
      id = id.substr(1);
    }
    auto q = id.find('?');
    if (q != std::string::npos) {
      id = id.substr(0, q);
    }
    if (id.empty()) {
      Shot s;
      s.ok = false;
      s.trigger = u;
      s.host = host;
      s.machine = Machine::kInvalid;
      s.note = "chrome://interstitials/ listing is not a fireable state";
      return s;
    }
    return ResolveInterstitial(id);
  }

  if (host == kNetworkErrorsHost) {
    Shot s;
    s.ok = false;
    s.trigger = u;
    s.host = host;
    s.machine = Machine::kInvalid;
    s.path = "listing";
    s.note =
        "chrome://network-errors is the listing WebUI, not a URLLoader error";
    return s;
  }

  // StartNetworkErrorsURLLoader
  int net_error = kErrInvalidUrl;
  if (host == kDinoHost) {
    net_error = kErrInternetDisconnected;
  } else if (host == kNetworkErrorHost) {
    std::string code = path;
    if (!code.empty() && code[0] == '/') {
      code = code.substr(1);
    }
    if (code.empty()) {
      net_error = kErrInvalidUrl;
    } else {
      char* end = nullptr;
      long parsed = std::strtol(code.c_str(), &end, 10);
      if (end && *end == '\0') {
        int temp = static_cast<int>(parsed);
        if (Catalog::Get().IsValidNetworkErrorCode(temp)) {
          net_error = temp;
        } else {
          net_error = kErrInvalidUrl;
        }
      } else {
        net_error = kErrInvalidUrl;
      }
    }
  } else if (scheme == "chrome") {
    if (auto dep = Deputies::Get().FindUrl(u)) {
      return ShotFromDeputy(*dep, u);
    }
    Shot s;
    s.ok = false;
    s.trigger = u;
    s.host = host;
    s.scheme = "chrome";
    s.machine = Machine::kInvalid;
    s.failed_nav_commits = true;
    s.note =
        "chrome:// isolate; not a network-error URLLoader host. A failed "
        "load of this WebUI still commits chrome-error://chromewebdata/.";
    return s;
  } else {
    Shot s;
    s.ok = false;
    s.trigger = u;
    s.host = host;
    s.machine = Machine::kInvalid;
    s.note = "not a chrome:// error trigger";
    return s;
  }

  std::string trigger = u;
  if (host == kDinoHost && trigger.find("://") != std::string::npos) {
    trigger = std::string(kDinoURL);
  }
  return FillFromError(net_error, trigger, host);
}

}  // namespace ces
