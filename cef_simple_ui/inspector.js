(function () {
  const summary = document.getElementById("summary");
  const details = document.getElementById("details");
  const subscriptions = new Map();

  function postCommand(action, args) {
    if (!window.cefQuery) return;
    window.cefQuery({
      request: JSON.stringify({ action: action, args: args || {} }),
      onSuccess: function () {},
      onFailure: function (_code, message) {
        summary.textContent = message || "C++ command failed";
      }
    });
  }

  window.app = {
    action: postCommand,
    subscribe: function (name, callback) {
      subscriptions.set(name, callback);
    },
    ready: function () {
      postCommand("__vsgCef.ready", { panel: document.body.dataset.panel || "" });
    }
  };

  window.__vsgCef = {
    receiveState: function (name, data) {
      const callback = subscriptions.get(name);
      if (callback) callback(data);
    }
  };

  app.subscribe("objects", function (objects) {
    const items = Array.isArray(objects) ? objects : [];
    summary.textContent = items.length + " scene objects from C++";
    details.replaceChildren();

    items.forEach(function (object) {
      const row = document.createElement("section");
      row.className = "object-row";
      if (object.selected) row.classList.add("selected");

      const title = document.createElement("p");
      title.textContent = object.name + " properties (" + object.type + ")";

      const meta = document.createElement("div");
      meta.className = "meta";
      meta.innerHTML =
        "<span>ID</span><span>" + object.id + "</span>" +
        "<span>Position</span><span>" + object.position.map(function (value) {
          return Number(value).toFixed(2);
        }).join(", ") + "</span>";

      row.appendChild(title);
      row.appendChild(meta);
      details.appendChild(row);
    });
  });

  app.ready();
}());
