#include "lime/context_provider_main.h"

#include "whp/net/tcp_socket.h"
#include "whp/net/udp_socket.h"
#include "whp/punch/punch.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace lime {

namespace {

whp::net::Endpoint ParseHostPort(const std::string& s, uint16_t fallback) {
  whp::net::Endpoint e = whp::net::Endpoint::Loopback(fallback);
  auto colon = s.rfind(':');
  if (colon == std::string::npos) {
    if (!s.empty()) {
      e.host = s;
    }
    return e;
  }
  e.host = s.substr(0, colon);
  e.port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
  if (e.host.empty()) {
    e.host = "127.0.0.1";
  }
  return e;
}

// One byte at a time is fine here: the control plane exchanges exactly two
// short lines per request, never a hot path.
bool RecvLine(whp::net::TcpSocket* sock, std::string* line) {
  line->clear();
  for (;;) {
    char c;
    int n = sock->Recv(&c, 1);
    if (n <= 0) {
      return !line->empty();
    }
    if (c == '\n') {
      return true;
    }
    if (c != '\r') {
      line->push_back(c);
    }
  }
}

bool SendLine(whp::net::TcpSocket* sock, const std::string& body) {
  std::string out = body + "\n";
  return sock->Send(out.data(), out.size()) == static_cast<int>(out.size());
}

}  // namespace

int ContextProviderMain(const ContextProviderOptions& options,
                        const ContextFactory& factory) {
  whp::net::TcpSocket listener;
  auto bind_ep = ParseHostPort(options.control_bind, 0);
  if (listener.Bind(bind_ep) != WHP_RESULT_OK) {
    std::fprintf(stderr, "lime::ContextProviderMain: bind %s failed\n",
                options.control_bind.c_str());
    return WHP_RESULT_INTERNAL;
  }
  if (listener.Listen() != WHP_RESULT_OK) {
    std::fprintf(stderr, "lime::ContextProviderMain: listen failed\n");
    return WHP_RESULT_INTERNAL;
  }
  auto local = listener.LocalEndpoint();
  std::printf("lime context-provider listening on %s:%u\n", local.host.c_str(),
             local.port);
  std::fflush(stdout);

  for (int minted = 0; options.max_contexts < 0 || minted < options.max_contexts;) {
    whp::net::TcpSocket conn;
    whp::net::Endpoint peer_tcp;
    if (listener.Accept(&conn, &peer_tcp) != WHP_RESULT_OK) {
      continue;
    }

    std::string line;
    if (!RecvLine(&conn, &line) || line.rfind("CREATE ", 0) != 0) {
      continue;
    }
    whp::net::Endpoint client_udp = ParseHostPort(line.substr(7), 0);

    whp::net::UdpSocket provider_udp;
    if (provider_udp.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK) {
      SendLine(&conn, "ERROR bind");
      continue;
    }
    if (!SendLine(&conn, "PORT " +
                             std::to_string(provider_udp.LocalEndpoint().port))) {
      continue;
    }
    conn.Close();

    std::vector<whp::punch::Candidate> remotes;
    remotes.push_back({client_udp, whp::punch::CandidateType::kHost});
    whp::punch::ConnectedPath path;
    if (whp::punch::Punch(provider_udp, remotes, &path) != WHP_RESULT_OK) {
      std::fprintf(stderr, "lime::ContextProviderMain: punch to %s:%u failed\n",
                  client_udp.host.c_str(), client_udp.port);
      continue;
    }

    whp::platform::Invitation invitation;
    if (whp::platform::InviteOver(std::move(path), &invitation,
                                  /*is_offerer=*/true) != WHP_RESULT_OK) {
      std::fprintf(stderr, "lime::ContextProviderMain: invite failed\n");
      continue;
    }

    ++minted;
    if (factory) {
      factory(std::move(invitation));
    }
  }
  return WHP_RESULT_OK;
}

WhpResult RequestContext(const std::string& control_address,
                         whp::platform::Invitation* out) {
  whp::net::TcpSocket conn;
  auto ep = ParseHostPort(control_address, 0);
  if (conn.Connect(ep) != WHP_RESULT_OK) {
    std::fprintf(stderr, "lime::RequestContext: connect %s:%u failed\n",
                ep.host.c_str(), ep.port);
    return WHP_RESULT_UNAVAILABLE;
  }

  whp::net::UdpSocket client_udp;
  if (client_udp.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK) {
    return WHP_RESULT_INTERNAL;
  }
  auto client_local = client_udp.LocalEndpoint();

  if (!SendLine(&conn, "CREATE 127.0.0.1:" +
                          std::to_string(client_local.port))) {
    return WHP_RESULT_UNAVAILABLE;
  }

  std::string line;
  if (!RecvLine(&conn, &line) || line.rfind("PORT ", 0) != 0) {
    return WHP_RESULT_UNAVAILABLE;
  }
  conn.Close();
  auto provider_ep =
      whp::net::Endpoint::Loopback(static_cast<uint16_t>(std::atoi(line.c_str() + 5)));

  std::vector<whp::punch::Candidate> remotes;
  remotes.push_back({provider_ep, whp::punch::CandidateType::kHost});
  whp::punch::ConnectedPath path;
  if (whp::punch::Punch(client_udp, remotes, &path) != WHP_RESULT_OK) {
    return WHP_RESULT_UNAVAILABLE;
  }

  return whp::platform::InviteOver(std::move(path), out, /*is_offerer=*/false);
}

}  // namespace lime
