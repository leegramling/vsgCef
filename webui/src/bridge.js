const subscriptions = new Map();
const stateCache = new Map();
const panelId = document.body?.dataset.panel || "";

function action(name, args = {}) {
  if (!window.cefQuery) return;
  window.cefQuery({
    request: JSON.stringify({ action: name, args }),
    onSuccess() {},
    onFailure(_code, message) {
      window.dispatchEvent(new CustomEvent("vsgcef-error", { detail: message || "C++ command failed" }));
    }
  });
}

export const app = {
  action,
  subscribe(name, callback) {
    subscriptions.set(name, callback);
    if (stateCache.has(name)) callback(stateCache.get(name));
    return () => subscriptions.delete(name);
  },
  ready() {
    action("__vsgCef.ready", { panel: panelId });
  }
};

window.__vsgCef = {
  receiveState(name, data) {
    stateCache.set(name, data);
    subscriptions.get(name)?.(data);
  }
};
