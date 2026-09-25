#ifndef CES_CDP_H_
#define CES_CDP_H_

#include "ces/types.h"

#include <cstdint>
#include <string>

namespace ces {

// CDP Runtime.evaluate wrapping a HolePunch FireToken.
// Occupy is chrome://network-error/-106 (not dino). Drive is WASMHolePunch.
// SRPC magics are fired, not rejected.
struct CdpFireResult {
  bool ok = false;
  std::string via;
  std::string method;
  uint32_t ordinal = 0;
  bool is_response = false;
  uint32_t bindings_version = 0;
  std::string occupy_trigger;
  std::string committed;
  std::string request_json;
  std::string result_json;
  std::string error;

  std::string ToJson() const;
};

std::string CdpEvaluateRequest(uint32_t id, uint32_t ordinal);
std::string CdpEvaluateResult(uint32_t id, const CdpFireResult& fire);

// Punch a loopback invitation, Fire(ordinal), Recv the response.
// QueryVersion (0xFFFFFFFF) auto-replies with bindings_version = 3.
CdpFireResult FireOrdinalCdp(uint64_t token);

}  // namespace ces

#endif  // CES_CDP_H_
