#include "ces/deputy.h"

#include "ces/loader.h"

#include <string>

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

std::string Lower(std::string_view s) {
  std::string o(s);
  for (char& c : o) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return o;
}

}  // namespace

std::string Deputy::ToJson() const {
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
  auto flag = [&](const char* k, bool v, bool comma) {
    if (comma) {
      o += ',';
    }
    o += '"';
    o += k;
    o += "\":";
    o += v ? "true" : "false";
  };
  field("id", id, false);
  field("scheme", scheme, true);
  field("origin", origin, true);
  field("url", url, true);
  field("access", AccessName(access), true);
  flag("chromewebdata", access == Access::kCommit || access == Access::kIs, true);
  flag("failed_nav_commits", failed_nav_commits, true);
  flag("subframe_leaks", subframe_leaks, true);
  field("downstream", DownstreamName(downstream), true);
  field("component_id", component_id, true);
  field("caller", caller, true);
  field("note", note, true);
  o += '}';
  return o;
}

Deputies::Deputies() {
  auto add = [&](const char* id, const char* scheme, const char* origin,
                 const char* url, Access access, Downstream down, bool failed,
                 bool leak, const char* component_id, const char* caller,
                 const char* note) {
    Deputy d;
    d.id = id;
    d.scheme = scheme;
    d.origin = origin;
    d.url = url;
    d.access = access;
    d.downstream = down;
    d.failed_nav_commits = failed;
    d.subframe_leaks = leak;
    d.component_id = component_id;
    d.caller = caller;
    d.note = note;
    all_.push_back(d);
  };

  // Shared unreachable origin. Display-isolated chrome-error scheme.
  add("chromewebdata", "chrome-error", "chrome-error://chromewebdata",
      "chrome-error://chromewebdata/", Access::kIs, Downstream::kOccupy, true,
      true, "", "StartNetworkErrorsURLLoader / failed nav",
      "kUnreachableWebDataURL. Occupy this origin, then drive with "
      "WASMHolePunch (Mojo Fire) and WASMMsgProxy (sendMessage). EV is not "
      "required. Subframe errors commit this URL in the embedder process.");
  add("deprecated-data", "data", "data:text/html,chromewebdata",
      "data:text/html,chromewebdata", Access::kIs, Downstream::kNone, false,
      false, "", "",
      "Pre-2017 unreachable URL. Replaced so error pages stop inheriting "
      "parent CSP. Still recognized as chromewebdata.");

  // Triggers that COMMIT chromewebdata via StartNetworkErrorsURLLoader.
  add("dino", "chrome", "chrome://dino", "chrome://dino/", Access::kCommit,
      Downstream::kOccupy, true, true, "", "chrome://dino",
      "Optional alias for chrome://network-error/-106. Not required. Same "
      "URLLoader as any network-error code.");
  add("network-error", "chrome", "chrome://network-error",
      "chrome://network-error/-106", Access::kCommit, Downstream::kOccupy, true,
      true, "", "chrome://network-error/<N>",
      "Any valid net error code. Same URLLoader, same chromewebdata commit.");

  // chrome:// isolates that do NOT commit chromewebdata unless THEY fail.
  add("interstitials", "chrome", "chrome://interstitials",
      "chrome://interstitials/", Access::kNone, Downstream::kNone, true, false,
      "", "",
      "Successful chrome:// WebUI preview. Own isolate. Not chromewebdata.");
  add("offline", "chrome", "chrome://offline", "chrome://offline/",
      Access::kNone, Downstream::kNone, true, false, "", "",
      "Offline interstitial cousin. chrome:// isolate.");
  add("lumberhack", "chrome", "chrome://contextual-tasks",
      "chrome://contextual-tasks/", Access::kNone, Downstream::kOccupy, true,
      false, "", "chrome://contextual-tasks",
      "Second deputy. chrome:// isolate the downstream document can occupy "
      "and execute in. Not chromewebdata.");
  add("ntp", "chrome", "chrome://new-tab-page", "chrome://new-tab-page/",
      Access::kNone, Downstream::kNone, true, false, "", "",
      "Trusted NTP WebUI. chrome:// isolate.");
  add("chrome-signin", "chrome", "chrome://chrome-signin",
      "chrome://chrome-signin/", Access::kNone, Downstream::kNone, true, false,
      "", "", "Gaia sign-in WebUI. chrome:// isolate.");
  add("traces", "chrome", "chrome://traces-internals",
      "chrome://traces-internals/", Access::kNone, Downstream::kNone, true,
      false, "", "", "Traces internals WebUI. chrome:// isolate.");
  add("chrome-urls", "chrome", "chrome://chrome-urls", "chrome://chrome-urls/",
      Access::kNone, Downstream::kNone, true, false, "", "",
      "chrome:// directory. chrome:// isolate.");
  add("print", "chrome", "chrome://print", "chrome://print/", Access::kNone,
      Downstream::kOccupy, true, false, "mhjfbmdgcfjbbpaeojofohoefgiehjai",
      "chrome://print postMessage",
      "Print preview WebUI. PdfScriptingApi postMessage into the PDF Viewer "
      "component. Other origins are dropped.");

  // Other display-isolated schemes.
  add("local-ntp", "chrome-search", "chrome-search://local-ntp",
      "chrome-search://local-ntp/", Access::kNone, Downstream::kNone, true,
      false, "", "",
      "chrome-search isolate. Failed NTP load → chromewebdata.");
  add("untrusted-ntp", "chrome-untrusted", "chrome-untrusted://new-tab-page",
      "chrome-untrusted://new-tab-page/", Access::kNone, Downstream::kNone,
      true, false, "", "",
      "chrome-untrusted isolate. Processes untrustworthy content.");
  add("devtools", "devtools", "devtools://devtools",
      "devtools://devtools/bundled/inspector.html", Access::kNone,
      Downstream::kNone, true, false, "", "", "DevTools isolate.");
  add("iwa", "isolated-app", "isolated-app://", "isolated-app://", Access::kNone,
      Downstream::kNone, true, false, "", "", "IWA domain-isolated origin.");
  add("scheme", "web+rbi", "web+rbi://listen", "web+rbi://listen", Access::kNone,
      Downstream::kNone, true, false, "", "",
      "Custom scheme re-entry. Failed load → chromewebdata.");

  // Drive buses. Occupy chromewebdata (or any other isolate), then Fire.
  // WASMHolePunch is Mojo; WASMMsgProxy is chrome.runtime.sendMessage.
  // EV MessageServiceHost is an optional SRPC host on the MsgProxy envelope.
  add("holepunch", "whp", "whp://invitation", "whp://invitation/fire",
      Access::kEmbed, Downstream::kDrive, false, false, "",
      "Invitation::Fire(ordinal) Mojo header.name",
      "WASMHolePunch. Sockets → punch → Mojo invitation. Drive from any "
      "occupied origin. Rejects EV SRPC (token does not fit uint32).");
  add("msgproxy", "wmp", "wmp://voodoo", "wmp://voodoo/sendMessage",
      Access::kEmbed, Downstream::kDrive, false, false, "",
      "chrome.runtime.sendMessage envelope",
      "WASMMsgProxy. The bus HolePunch refuses. Voodoo mints SRPC keys "
      "(not only SECRPC). Hosts: runtime | offscreen | SW | tab. Forwards "
      "{ordinal,name,args} onto HolePunch.");
  add("ev", "chrome-extension",
      "chrome-extension://callobklhcbilhphinckomhgkigmfocg",
      "chrome-extension://callobklhcbilhphinckomhgkigmfocg/", Access::kEmbed,
      Downstream::kDrive, true, false, "callobklhcbilhphinckomhgkigmfocg",
      "optional MessageServiceHost(*) — same envelope as msgproxy",
      "Optional. EV is one SRPC host WASMMsgProxy already speaks. Not "
      "required when holepunch + msgproxy are present.");
  add("pdf", "chrome-extension",
      "chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai",
      "chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/", Access::kEmbed,
      Downstream::kOccupy, true, false, "mhjfbmdgcfjbbpaeojofohoefgiehjai",
      "application/pdf mime handler, chrome://print",
      "PDF Viewer. mime_types_handler: a PDF navigation occupies this "
      "extension origin. Closest component analog of lumberhack.");
  add("hangouts", "chrome-extension",
      "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome",
      "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/", Access::kEmbed,
      Downstream::kDrive, true, false, "nkeimhogjdpnpccoofpliimaahmaaome",
      "meet.google.com / hangouts",
      "Google Hangouts background_page (always on). Meet pages drive "
      "WebRTC/hardware APIs. Lure cannot occupy this origin.");
  add("tts", "chrome-extension",
      "chrome-extension://fignfifoniblkonapihmkfakmlgkbkcf",
      "chrome-extension://fignfifoniblkonapihmkfakmlgkbkcf/", Access::kEmbed,
      Downstream::kDrive, true, false, "fignfifoniblkonapihmkfakmlgkbkcf",
      "speechSynthesis / chrome.tts",
      "Built-in TTS engine service_worker (always on). Web Speech / chrome.tts "
      "runs inside this SW. Page origin does not change.");
  add("wasmtts", "chrome-extension",
      "chrome-extension://bjbcblmdcnggnibecjikpoljcgkbgphl",
      "chrome-extension://bjbcblmdcnggnibecjikpoljcgkbgphl/", Access::kEmbed,
      Downstream::kDrive, true, false, "bjbcblmdcnggnibecjikpoljcgkbgphl",
      "chrome.ttsEngine speak",
      "WasmTtsEngine component. Mojo JS bindings. Dagger surface includes "
      "speak. Drive via TTS, not occupy.");
  add("glic", "chrome-extension",
      "chrome-extension://admccjkmockfdflocgggjfgdacdodkdf",
      "chrome-extension://admccjkmockfdflocgggjfgdacdodkdf/", Access::kEmbed,
      Downstream::kDrive, true, false, "admccjkmockfdflocgggjfgdacdodkdf",
      "gemini.google.com + documentId",
      "Glic. externally_connectable requires a real documentId. Sticky "
      "https://gemini.google.com tab, not an arbitrary lure origin.");
  add("docs", "chrome-extension",
      "chrome-extension://ghbmnnjooekpmoecnnnilnnbdlolhkhi",
      "chrome-extension://ghbmnnjooekpmoecnnnilnnbdlolhkhi/", Access::kEmbed,
      Downstream::kDrive, true, false, "ghbmnnjooekpmoecnnnilnnbdlolhkhi",
      "docs.google.com / drive.google.com",
      "Docs Offline service worker. Owns docs/drive origins. Drive from "
      "those pages, not from a lure.");
  add("payments", "chrome-extension",
      "chrome-extension://nmmhkkegccagdldgiimedpiccmgmieda",
      "chrome-extension://nmmhkkegccagdldgiimedpiccmgmieda/", Access::kEmbed,
      Downstream::kDrive, true, false, "nmmhkkegccagdldgiimedpiccmgmieda",
      "Chrome Web Store payments",
      "CWS Payments MV2 app. Soft presence probe, not an occupy deputy.");
  add("cryptotoken", "chrome-extension",
      "chrome-extension://kmendfapggjehodndflmmgagdbamhnfd",
      "chrome-extension://kmendfapggjehodndflmmgagdbamhnfd/", Access::kEmbed,
      Downstream::kDrive, true, false, "kmendfapggjehodndflmmgagdbamhnfd",
      "navigator.credentials / WebAuthn any RP",
      "CryptoTokenExtension. Built-in U2F/WebAuthn component. Any RP origin "
      "drives it through the WebAuthn API. Does not occupy the extension origin.");
  add("cast", "chrome-extension",
      "chrome-extension://pkedcjkdefgpdelpbcmbmeomcjaceakc",
      "chrome-extension://pkedcjkdefgpdelpbcmbmeomcjaceakc/", Access::kEmbed,
      Downstream::kDrive, true, false, "pkedcjkdefgpdelpbcmbmeomcjaceakc",
      "presentation / cast",
      "Cast component. Presentation API / media router. Drive from a page "
      "that starts a presentation.");
  add("webstore", "chrome-extension",
      "chrome-extension://ahfgeienlihckogmohjhadlkjgocpleb",
      "chrome-extension://ahfgeienlihckogmohjhadlkjgocpleb/", Access::kEmbed,
      Downstream::kDrive, true, false, "ahfgeienlihckogmohjhadlkjgocpleb",
      "chrome.google.com/webstore",
      "Chrome Web Store component. Drive from CWS origins.");
  add("feedback", "chrome-extension",
      "chrome-extension://gfdkimpbcpahaombhbimeihdjnejgicl",
      "chrome-extension://gfdkimpbcpahaombhbimeihdjnejgicl/", Access::kEmbed,
      Downstream::kDrive, true, false, "gfdkimpbcpahaombhbimeihdjnejgicl",
      "chrome://feedback",
      "Feedback component. chrome://feedback occupies the WebUI; this "
      "extension is the helper.");

  add("glic-ui", "chrome", "chrome://glic", "chrome://glic/", Access::kNone,
      Downstream::kOccupy, true, false, "admccjkmockfdflocgggjfgdacdodkdf",
      "chrome://glic",
      "Glic privileged WebUI. Occupying this chrome:// isolate is the panel "
      "shell. The SW deputy is glic.");
  add("glic-guest", "chrome-untrusted", "chrome-untrusted://glic",
      "chrome-untrusted://glic/", Access::kNone, Downstream::kOccupy, true,
      false, "admccjkmockfdflocgggjfgdacdodkdf", "chrome-untrusted://glic",
      "Glic guest webview. chrome-untrusted isolate that renders Gemini UI.");
  add("extensions", "chrome", "chrome://extensions", "chrome://extensions/",
      Access::kNone, Downstream::kOccupy, true, false, "",
      "chrome://extensions", "Extensions manager WebUI.");
  add("inspect", "chrome", "chrome://inspect", "chrome://inspect/",
      Access::kNone, Downstream::kOccupy, true, false, "", "chrome://inspect",
      "Inspect WebUI. CDP target list.");
  add("sw-internals", "chrome", "chrome://serviceworker-internals",
      "chrome://serviceworker-internals/", Access::kNone, Downstream::kOccupy,
      true, false, "", "chrome://serviceworker-internals",
      "Service worker fleet start/stop WebUI.");
  add("web-app-internals", "chrome", "chrome://web-app-internals",
      "chrome://web-app-internals/", Access::kNone, Downstream::kOccupy, true,
      false, "", "chrome://web-app-internals",
      "IWA/PWA internals. installIsolatedWebAppFromDevProxy lives here.");
  add("net-internals", "chrome", "chrome://net-internals",
      "chrome://net-internals/", Access::kNone, Downstream::kOccupy, true,
      false, "", "chrome://net-internals", "Net internals WebUI.");
  add("flags", "chrome", "chrome://flags", "chrome://flags/", Access::kNone,
      Downstream::kOccupy, true, false, "", "chrome://flags",
      "Feature flags WebUI.");
  add("policy", "chrome", "chrome://policy", "chrome://policy/", Access::kNone,
      Downstream::kOccupy, true, false, "", "chrome://policy",
      "Policy WebUI.");
  add("gpu", "chrome", "chrome://gpu", "chrome://gpu/", Access::kNone,
      Downstream::kOccupy, true, false, "", "chrome://gpu", "GPU internals.");
  add("skills", "chrome", "chrome://skills", "chrome://skills/", Access::kNone,
      Downstream::kOccupy, true, false, "", "chrome://skills",
      "Skills WebUI. Consumer of chrome://image WebP wrap.");
  add("actor-overlay", "chrome", "chrome://actor-overlay",
      "chrome://actor-overlay/", Access::kNone, Downstream::kOccupy, true,
      false, "", "chrome://actor-overlay", "Actor overlay WebUI.");
  add("image", "chrome", "chrome://image", "chrome://image/", Access::kNone,
      Downstream::kNone, true, false, "", "SanitizedImageSource wrap",
      "chrome://image is not a document. ImageNavigationThrottle blocks "
      "document nav. Wrap for other chrome:// WebUIs.");
  add("feedback-ui", "chrome", "chrome://feedback", "chrome://feedback/",
      Access::kNone, Downstream::kOccupy, true, false,
      "gfdkimpbcpahaombhbimeihdjnejgicl", "chrome://feedback",
      "Feedback WebUI. Pairs with the feedback component.");
  add("blank", "about", "about:blank", "about:blank", Access::kNone,
      Downstream::kOccupy, false, false, "", "about:blank",
      "about:blank persist surface. Downstream-only; not chromewebdata.");
  add("srcdoc", "about", "about:srcdoc", "about:srcdoc", Access::kNone,
      Downstream::kOccupy, false, false, "", "about:srcdoc",
      "about:srcdoc persist surface.");
  add("scheme-gemini", "web+gemini", "web+gemini://listen",
      "web+gemini://listen", Access::kNone, Downstream::kNone, true, false, "",
      "", "Custom scheme re-entry (gemini). Failed load → chromewebdata.");
  add("scheme-iwa", "web+iwa", "web+iwa://listen", "web+iwa://listen",
      Access::kNone, Downstream::kNone, true, false, "", "",
      "Custom scheme re-entry (iwa). Failed load → chromewebdata.");
  add("scheme-cadidum", "web+cadidum", "web+cadidum://listen",
      "web+cadidum://listen", Access::kNone, Downstream::kNone, true, false, "",
      "", "Custom scheme re-entry (cadidum). Failed load → chromewebdata.");
}

const Deputies& Deputies::Get() {
  static Deputies d;
  return d;
}

std::vector<Deputy> Deputies::WithDownstream(Downstream d) const {
  std::vector<Deputy> out;
  for (const auto& x : all_) {
    if (x.downstream == d) {
      out.push_back(x);
    }
  }
  return out;
}

std::string Deputies::DumpJson() const {
  std::string o = "[";
  for (std::size_t i = 0; i < all_.size(); ++i) {
    if (i) {
      o += ',';
    }
    o += all_[i].ToJson();
  }
  o += ']';
  return o;
}

std::optional<Deputy> Deputies::FindId(std::string_view id) const {
  for (const auto& d : all_) {
    if (id == d.id || (d.component_id[0] && id == d.component_id)) {
      return d;
    }
  }
  return std::nullopt;
}

std::optional<Deputy> Deputies::FindUrl(std::string_view url) const {
  std::string u = Lower(url);
  std::string host = Loader::HostOf(u);
  std::string scheme;
  auto sep = u.find("://");
  if (sep != std::string::npos) {
    scheme = u.substr(0, sep);
  }
  if (u.rfind("data:text/html,chromewebdata", 0) == 0) {
    return FindId("deprecated-data");
  }
  for (const auto& d : all_) {
    if (u == Lower(d.url) || u == Lower(d.origin) ||
        u.rfind(Lower(d.url), 0) == 0) {
      return d;
    }
    if (scheme == d.scheme && host == Loader::HostOf(d.url)) {
      return d;
    }
    if (scheme == "chrome-extension" && d.scheme == std::string("chrome-extension") &&
        host == Loader::HostOf(d.url)) {
      return d;
    }
  }
  if (scheme == "chrome-extension" && !host.empty()) {
    Deputy generic;
    generic.id = "chrome-extension";
    generic.scheme = "chrome-extension";
    generic.origin = "chrome-extension://";
    generic.url = url.data();
    generic.access = Access::kEmbed;
    generic.downstream = Downstream::kDrive;
    generic.failed_nav_commits = true;
    generic.component_id = "";
    generic.note =
        "Per-id chrome-extension isolate. Resource policy allows "
        "chrome-error://chromewebdata (error-page icon). Failed extension "
        "navigation commits chromewebdata.";
    return generic;
  }
  if (scheme == "chrome-untrusted" && !host.empty()) {
    Deputy generic;
    generic.id = "chrome-untrusted";
    generic.scheme = "chrome-untrusted";
    generic.origin = "chrome-untrusted://";
    generic.url = url.data();
    generic.access = Access::kNone;
    generic.failed_nav_commits = true;
    generic.note =
        "Per-host chrome-untrusted isolate. Failed load commits chromewebdata.";
    return generic;
  }
  if (scheme == "chrome" && !host.empty() && host != "dino" &&
      host != "network-error" && host != "network-errors" &&
      host != "interstitials") {
    Deputy generic;
    generic.id = "chrome-internal";
    generic.scheme = "chrome";
    generic.origin = "chrome://";
    generic.url = url.data();
    generic.access = Access::kNone;
    generic.downstream = Downstream::kNone;
    generic.failed_nav_commits = true;
    generic.note =
        "chrome:// internal page. Own isolate. A failed load commits "
        "chrome-error://chromewebdata/.";
    return generic;
  }
  if (scheme == "chrome-search" && !host.empty()) {
    Deputy generic;
    generic.id = "chrome-search";
    generic.scheme = "chrome-search";
    generic.origin = "chrome-search://";
    generic.url = url.data();
    generic.access = Access::kNone;
    generic.failed_nav_commits = true;
    generic.note =
        "chrome-search isolate. Failed load commits chromewebdata.";
    return generic;
  }
  return std::nullopt;
}

Shot ShotFromDeputy(const Deputy& d, std::string_view trigger) {
  Shot s;
  s.ok = true;
  s.trigger = std::string(trigger.empty() ? d.url : trigger);
  s.host = Loader::HostOf(s.trigger);
  s.scheme = d.scheme;
  s.deputy = d.id;
  s.access = d.access;
  s.failed_nav_commits = d.failed_nav_commits;
  s.subframe_leaks = d.subframe_leaks;
  s.downstream = d.downstream;
  s.component_id = d.component_id;
  s.caller = d.caller;
  s.path = "isolated_scheme";
  s.note = d.note;
  s.name = d.id;
  s.error_short = d.id;
  s.heading = d.origin;
  s.summary = d.note;
  if (d.downstream == Downstream::kOccupy) {
    s.path = "downstream_occupy";
  } else if (d.downstream == Downstream::kDrive) {
    s.path = "downstream_drive";
  }
  if (d.access == Access::kIs || d.access == Access::kCommit) {
    s.chromewebdata = true;
    s.committed = std::string(kChromewebdataURL);
    s.machine = Machine::kNeterror;
    s.offline_class = true;
    if (d.access == Access::kIs) {
      s.path = "committed_chromewebdata";
      s.net_error = kErrInternetDisconnected;
      s.name = "ERR_INTERNET_DISCONNECTED";
      s.shows_dino = true;
    }
  } else if (d.access == Access::kEmbed) {
    s.chromewebdata = false;
    s.committed = d.origin;
  } else {
    s.chromewebdata = false;
    s.committed = d.origin;
    if (std::string(d.id) == "chrome-internal") {
      s.committed = s.trigger;
      if (!s.committed.empty() && s.committed.back() != '/') {
        s.committed.push_back('/');
      }
    }
  }
  return s;
}

}  // namespace ces
