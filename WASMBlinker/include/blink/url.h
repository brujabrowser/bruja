#ifndef BLINK_URL_H_
#define BLINK_URL_H_

#include <cstdint>
#include <string>

namespace blink {

// A minimal, purpose-built URL splitter -- not a general URL parser.
// Handles exactly "scheme://host[:port][/path]" (the shape every URL this
// stack navigates to actually has -- see LocalFrameImpl::Navigate). No
// query-string/fragment handling beyond leaving them inside `path`
// verbatim, no percent-decoding, no IPv6 host literals.
struct ParsedUrl {
  std::string scheme;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";
};

// Returns false if `url` doesn't even have a "scheme://host" shape.
// `port` defaults to 80 for "http", 443 for "https", 0 (caller must reject)
// for anything else.
bool ParseUrl(const std::string& url, ParsedUrl* out);

// Resolves `ref` against `base` (Blink's basic absolute-URL algorithm
// for http(s) documents): absolute refs pass through; `/path` is host-
// relative; anything else is relative to the base path's directory.
std::string ResolveUrl(const std::string& base, const std::string& ref);

}  // namespace blink

#endif  // BLINK_URL_H_
