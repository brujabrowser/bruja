#include "blink/url.h"

#include <cstdlib>

namespace blink {

bool ParseUrl(const std::string& url, ParsedUrl* out) {
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return false;
  out->scheme = url.substr(0, scheme_end);

  std::string rest = url.substr(scheme_end + 3);
  auto path_start = rest.find('/');
  std::string hostport = path_start == std::string::npos ? rest : rest.substr(0, path_start);
  out->path = path_start == std::string::npos ? "/" : rest.substr(path_start);
  if (out->path.empty()) out->path = "/";

  auto colon = hostport.rfind(':');
  if (colon == std::string::npos) {
    out->host = hostport;
    out->port = out->scheme == "https" ? 443 : (out->scheme == "http" ? 80 : 0);
  } else {
    out->host = hostport.substr(0, colon);
    out->port = static_cast<uint16_t>(std::atoi(hostport.c_str() + colon + 1));
  }
  return !out->host.empty();
}

std::string ResolveUrl(const std::string& base, const std::string& ref) {
  std::string r = ref;
  if (r.empty()) return base;
  if (r.find("://") != std::string::npos) return r;
  ParsedUrl b;
  if (!ParseUrl(base, &b)) return r;
  auto hostport = [&]() {
    std::string hp = b.host;
    bool default_port =
        (b.scheme == "http" && b.port == 80) || (b.scheme == "https" && b.port == 443);
    if (!default_port && b.port != 0) hp += ":" + std::to_string(b.port);
    return hp;
  };
  if (r.size() >= 2 && r[0] == '/' && r[1] == '/') return b.scheme + ":" + r;
  if (r[0] == '/') return b.scheme + "://" + hostport() + r;
  std::string dir = b.path;
  auto slash = dir.rfind('/');
  dir = slash == std::string::npos ? "/" : dir.substr(0, slash + 1);
  return b.scheme + "://" + hostport() + dir + r;
}

}  // namespace blink
