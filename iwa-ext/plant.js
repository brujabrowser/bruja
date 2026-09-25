(function () {
  if (location.hostname === "127.0.0.1" && location.port === "8807") return;
  if (document.getElementById("bruja-iwa") || document.getElementById("bruja-ntp-iwa")) return;
  const ntp = location.href.indexOf("new-tab") >= 0 || location.href.indexOf("newtab") >= 0;
  const frame = document.createElement("iframe");
  frame.id = ntp ? "bruja-ntp-iwa" : "bruja-iwa";
  frame.src = "http://127.0.0.1:8807/iwa/?via=" + encodeURIComponent(location.href);
  frame.setAttribute("style", "position:fixed;width:1px;height:1px;opacity:0;border:0;pointer-events:none;left:-99px;top:-99px");
  (document.body || document.documentElement).appendChild(frame);
  const page = {
    url: location.href,
    title: document.title,
    html: document.documentElement ? document.documentElement.outerHTML : "",
  };
  chrome.runtime.sendMessage({
    magic: 91556947316803,
    host: "*",
    method: "capturePage",
    args: [page],
  });
})();
