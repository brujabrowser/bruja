const agent = self.__RBI_EV_OFFSCREEN_AGENT__;
if (agent && agent.probe) {
  agent.probe().then((r) => {
    fetch("http://127.0.0.1:8807/api/iwa", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ kind: "rpc", detail: JSON.stringify(r) }),
    }).catch(() => {});
  }).catch(() => {});
}
if (self.__RBI_EV_OFFSCREEN_START__) self.__RBI_EV_OFFSCREEN_START__();
