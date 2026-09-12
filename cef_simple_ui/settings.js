(function () {
  const summary = document.getElementById("summary");
  const bridge = document.getElementById("bridge");
  const count = document.getElementById("count");
  const sceneFps = document.getElementById("sceneFps");
  const cefFps = document.getElementById("cefFps");
  const selected = document.getElementById("selected");
  const subscriptions = new Map();

  function postCommand(action, args) {
    if (window.cefQuery) {
      window.cefQuery({
        request: JSON.stringify({ action: action, args: args || {} }),
        onSuccess: function () {},
        onFailure: function (_code, message) {
          summary.textContent = message || "C++ command failed";
        }
      });
    }
  }

  window.app = {
    action: postCommand,
    subscribe: function (name, callback) { subscriptions.set(name, callback); },
    ready: function () { postCommand("__vsgCef.ready", { panel: document.body.dataset.panel || "" }); }
  };

  window.__vsgCef = {
    receiveState: function (name, data) {
      const callback = subscriptions.get(name);
      if (callback) callback(data);
    }
  };

  app.subscribe("renderStatus", function (status) {
    sceneFps.value = Number(status.sceneFps || 0).toFixed(1);
    cefFps.value = Number(status.cefFps || 0).toFixed(1);
    selected.value = status.selectedObject || "None";
    count.value = String(status.objectCount || 0);
    bridge.value = "connected";
    summary.textContent = "Render status connected to C++";
  });

  app.subscribe("objects", function (objects) {
    const total = Array.isArray(objects) ? objects.length : 0;
    bridge.value = "connected";
    count.value = String(total);
    summary.textContent = "Render status connected to C++";
  });

  app.ready();
}());
