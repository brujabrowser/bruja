#include "ces/catalog.h"

#include <algorithm>
#include <cctype>

namespace ces {
namespace {

std::string Upper(std::string_view s) {
  std::string o(s);
  for (char& c : o) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return o;
}

const char* kReach = "This site can't be reached";
const char* kCables =
    "Try checking the network cables, modem, and router.";
const char* kDns =
    "Check if there is a typo in the address, or try again later.";
const char* kRefused = "The connection was refused.";
const char* kReset = "The connection was reset.";
const char* kTimeout = "The connection timed out.";
const char* kBlocked = "This page has been blocked";
const char* kPrivate = "Your connection is not private";
const char* kAccess = "Your Internet access is blocked";

}  // namespace

Catalog::Catalog() {
  auto add = [&](int code, const char* name, const char* heading,
                 const char* summary, const char* error_short = "",
                 bool dino = false) {
    ErrorInfo e;
    e.code = code;
    e.name = name;
    e.heading = heading;
    e.summary = summary;
    e.error_short = error_short[0] ? error_short : name;
    e.shows_dino = dino;
    errors_.push_back(e);
  };

  // System
  add(-1, "ERR_IO_PENDING", kReach, "Asynchronous IO is not yet complete.");
  add(-2, "ERR_FAILED", kReach, "A generic failure occurred.");
  add(-3, "ERR_ABORTED", kReach, "The operation was aborted.");
  add(-7, "ERR_TIMED_OUT", kReach, kTimeout);
  add(-10, "ERR_ACCESS_DENIED", kAccess, "Permission to access a resource was denied.");
  add(-20, "ERR_BLOCKED_BY_CLIENT", kBlocked,
      "The client chose to block the request.");
  add(-21, "ERR_NETWORK_CHANGED", kReach, "The network changed.");
  add(-22, "ERR_BLOCKED_BY_ADMINISTRATOR", kBlocked,
      "The request was blocked by the URL block list.");
  add(-27, "ERR_BLOCKED_BY_RESPONSE", kBlocked,
      "The response was delivered with unmet security requirements.");
  add(-30, "ERR_BLOCKED_BY_CSP", kBlocked,
      "The request was blocked by a Content Security Policy.");
  add(-32, "ERR_BLOCKED_BY_ORB", kBlocked,
      "The request was blocked by CORB or ORB.");
  add(-33, "ERR_NETWORK_ACCESS_REVOKED", kAccess,
      "The request originated from a frame that has disabled network access.");

  // Connection — this is the family chrome://dino occupies.
  add(-100, "ERR_CONNECTION_CLOSED", kReach, "The connection was closed.");
  add(-101, "ERR_CONNECTION_RESET", kReach, kReset);
  add(-102, "ERR_CONNECTION_REFUSED", kReach, kRefused);
  add(-103, "ERR_CONNECTION_ABORTED", kReach, "The connection was aborted.");
  add(-104, "ERR_CONNECTION_FAILED", kReach, "A connection attempt failed.");
  add(-105, "ERR_NAME_NOT_RESOLVED", kReach, kDns);
  add(-106, "ERR_INTERNET_DISCONNECTED", "No internet", kCables,
      "ERR_NO_INTERNET", true);
  add(-107, "ERR_SSL_PROTOCOL_ERROR", kPrivate, "An SSL protocol error occurred.");
  add(-108, "ERR_ADDRESS_INVALID", kReach, "The IP address or port number is invalid.");
  add(-109, "ERR_ADDRESS_UNREACHABLE", kReach, "The IP address is unreachable.");
  add(-110, "ERR_SSL_CLIENT_AUTH_CERT_NEEDED", kPrivate,
      "The server requested a client certificate.");
  add(-111, "ERR_TUNNEL_CONNECTION_FAILED", kReach,
      "A tunnel connection through the proxy could not be established.");
  add(-113, "ERR_SSL_VERSION_OR_CIPHER_MISMATCH", kPrivate,
      "The client and server don't support a common SSL version or cipher.");
  add(-118, "ERR_CONNECTION_TIMED_OUT", kReach, kTimeout);
  add(-130, "ERR_PROXY_CONNECTION_FAILED", kReach,
      "Could not create a connection to the proxy server.");
  add(-137, "ERR_NAME_RESOLUTION_FAILED", kReach, kDns);
  add(-138, "ERR_NETWORK_ACCESS_DENIED", kAccess,
      "Permission to access the network was denied.");
  add(-166, "ERR_ICANN_NAME_COLLISION", kReach,
      "Resolving the hostname included the ICANN name-collision address.");
  add(-186, "ERR_PROXY_UNABLE_TO_CONNECT_TO_DESTINATION", kReach,
      "The proxy was unable to connect to the destination.");

  // Certificate — chrome://network-error/-20x still uses the neterror
  // URLLoader. Real TLS failures take the ssl_interstitial machine.
  add(-200, "ERR_CERT_COMMON_NAME_INVALID", kPrivate,
      "The certificate common name did not match the host name.");
  add(-201, "ERR_CERT_DATE_INVALID", kPrivate,
      "The certificate appears not yet valid or expired.");
  add(-202, "ERR_CERT_AUTHORITY_INVALID", kPrivate,
      "The certificate is signed by an authority we don't trust.");
  add(-203, "ERR_CERT_CONTAINS_ERRORS", kPrivate,
      "The certificate contains errors.");
  add(-206, "ERR_CERT_REVOKED", kPrivate, "The certificate has been revoked.");
  add(-207, "ERR_CERT_INVALID", kPrivate, "The certificate is invalid.");
  add(-217, "ERR_CERT_KNOWN_INTERCEPTION_BLOCKED", kPrivate,
      "The certificate is known to be used for interception.");
  add(-219, "ERR_CERT_SELF_SIGNED_LOCAL_NETWORK", kPrivate,
      "The certificate is self-signed on a local network name.");

  // HTTP
  add(-300, "ERR_INVALID_URL", kReach, "The URL is invalid.");
  add(-301, "ERR_DISALLOWED_URL_SCHEME", kReach, "The scheme of the URL is disallowed.");
  add(-310, "ERR_TOO_MANY_REDIRECTS", kReach, "Too many redirects.");
  add(-312, "ERR_UNSAFE_PORT", kReach, "Attempting to load a URL with an unsafe port.");
  add(-320, "ERR_INVALID_RESPONSE", kReach, "The server's response was invalid.");
  add(-324, "ERR_EMPTY_RESPONSE", kReach,
      "The server closed the connection without sending any data.");
  add(-337, "ERR_HTTP2_PROTOCOL_ERROR", kReach, "There is an HTTP/2 protocol error.");
  add(-356, "ERR_QUIC_PROTOCOL_ERROR", kReach, "There is a QUIC protocol error.");

  // Cache / other / DNS
  add(-400, "ERR_CACHE_MISS", kReach, "The cache does not have the requested entry.");
  add(-501, "ERR_INSECURE_RESPONSE", kPrivate,
      "The server's response was insecure.");
  add(-800, "ERR_DNS_MALFORMED_RESPONSE", kReach,
      "DNS resolver received a malformed response.");
  add(-803, "ERR_DNS_TIMED_OUT", kReach, "DNS transaction timed out.");
  add(-900, "ERR_BLOB_INVALID_CONSTRUCTION_ARGUMENTS", kReach,
      "The blob construction arguments are invalid.");
  add(-150, "ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN", kPrivate,
      "The server's certificate did not match any pinned keys.");
  add(-208, "ERR_CERT_WEAK_SIGNATURE_ALGORITHM", kPrivate,
      "The certificate used a weak signature algorithm.");
  add(-211, "ERR_CERT_WEAK_KEY", kPrivate, "The certificate used a weak key.");
  add(-212, "ERR_CERT_NAME_CONSTRAINT_VIOLATION", kPrivate,
      "The certificate name constraints were violated.");
  add(-311, "ERR_UNSAFE_REDIRECT", kReach, "The redirect was unsafe.");
  add(-330, "ERR_CONTENT_DECODING_FAILED", kReach,
      "Content decoding failed.");
  add(-380, "ERR_HTTP_RESPONSE_CODE_FAILURE", kReach,
      "The HTTP response code indicated failure.");

  auto add_i = [&](const char* id, const char* url, Machine m,
                   const char* heading, const char* note) {
    InterstitialInfo i;
    i.id = id;
    i.url = url;
    i.machine = m;
    i.heading = heading;
    i.note = note;
    interstitials_.push_back(i);
  };

  add_i("ssl", "chrome://interstitials/ssl", Machine::kSslInterstitial,
        kPrivate, "security_interstitials SSL preview, not neterror URLLoader");
  add_i("ssl-clock", "chrome://interstitials/ssl?clock=1",
        Machine::kSslInterstitial, kPrivate, "SSL clock-wrong preview");
  add_i("mitm-software", "chrome://interstitials/mitm-software",
        Machine::kSslInterstitial, kPrivate, "Known interception software");
  add_i("captiveportal", "chrome://interstitials/captiveportal",
        Machine::kCaptivePortal, "Connect to the network",
        "Captive portal interstitial preview");
  add_i("https_only", "chrome://interstitials/https_only",
        Machine::kHttpsOnly, kPrivate, "HTTPS-First Mode interstitial");
  add_i("insecure_form", "chrome://interstitials/insecure_form",
        Machine::kInsecureForm, "The form you are submitting is not secure",
        "Insecure form interstitial");
  add_i("enterprise-block", "chrome://interstitials/enterprise-block",
        Machine::kEnterprise, kBlocked, "Enterprise block interstitial");
  add_i("enterprise-warn", "chrome://interstitials/enterprise-warn",
        Machine::kEnterprise, kBlocked, "Enterprise warn interstitial");
  add_i("quiet", "chrome://interstitials/quiet", Machine::kSafeBrowsing,
        kBlocked, "Quiet Safe Browsing interstitial");
  add_i("malware", "chrome://interstitials/malware", Machine::kSafeBrowsing,
        kBlocked, "Safe Browsing malware interstitial");
  add_i("phishing", "chrome://interstitials/phishing", Machine::kSafeBrowsing,
        kBlocked, "Safe Browsing phishing interstitial");
  add_i("billing", "chrome://interstitials/billing", Machine::kSafeBrowsing,
        kBlocked, "Safe Browsing billing interstitial");
  add_i("lookalike", "chrome://interstitials/lookalike",
        Machine::kSslInterstitial, kPrivate, "Lookalike URL interstitial");
  add_i("supervised-user-verify", "chrome://interstitials/supervised-user-verify",
        Machine::kEnterprise, kBlocked, "Supervised-user verify interstitial");
}

const Catalog& Catalog::Get() {
  static Catalog c;
  return c;
}

std::optional<ErrorInfo> Catalog::FindCode(int code) const {
  for (const auto& e : errors_) {
    if (e.code == code) {
      return e;
    }
  }
  return std::nullopt;
}

std::optional<ErrorInfo> Catalog::FindName(std::string_view name) const {
  std::string n = Upper(name);
  if (n.rfind("ERR_", 0) != 0) {
    n = "ERR_" + n;
  }
  for (const auto& e : errors_) {
    if (e.name == n || e.error_short == n) {
      return e;
    }
  }
  return std::nullopt;
}

std::optional<InterstitialInfo> Catalog::FindInterstitial(
    std::string_view id) const {
  for (const auto& i : interstitials_) {
    if (id == i.id) {
      return i;
    }
  }
  return std::nullopt;
}

bool Catalog::IsValidNetworkErrorCode(int code) const {
  if (code == kErrIoPending) {
    return false;
  }
  return FindCode(code).has_value();
}

}  // namespace ces
