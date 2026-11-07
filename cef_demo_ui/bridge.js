(function () {
  const subscriptions = new Map();
  function action(name, args) {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: JSON.stringify({ action: name, args: args || {} }),
      onSuccess: function () {},
      onFailure: function (_code, message) {
        if (window.__vsgCefOnError) window.__vsgCefOnError(message || "C++ command failed");
      }
    });
  }
  window.app = {
    action,
    subscribe: function (name, callback) { subscriptions.set(name, callback); },
    ready: function () { action("__vsgCef.ready", { panel: document.body.dataset.panel || "" }); }
  };
  window.__vsgCef = { receiveState: function (name, data) {
    const callback = subscriptions.get(name); if (callback) callback(data);
  }};
}());
