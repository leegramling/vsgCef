(function () {
  const form = document.getElementById("objectForm");
  const status = document.getElementById("status");
  let objects = [];
  let lastObjectsJson = "";

  function postCommand(command) {
    const payload = JSON.stringify(command);
    if (window.cefQuery) {
      window.cefQuery({
        request: payload,
        onSuccess: function () {
          status.textContent = "Sent rename to C++";
        },
        onFailure: function (_code, message) {
          status.textContent = message || "C++ command failed";
        }
      });
    } else {
      status.textContent = "cefQuery is unavailable";
    }
  }

  function focusedNameState() {
    const active = document.activeElement;
    if (!active || active.dataset.role !== "object-name") return null;
    return {
      id: active.dataset.objectId,
      selectionStart: active.selectionStart,
      selectionEnd: active.selectionEnd
    };
  }

  function render() {
    const focused = focusedNameState();
    form.replaceChildren();
    status.textContent = objects.length + " objects from C++";

    objects.forEach(function (object) {
      const row = document.createElement("section");
      row.className = "object-row";

      const nameLabel = document.createElement("label");
      nameLabel.textContent = "Name";
      const name = document.createElement("input");
      name.dataset.role = "object-name";
      name.dataset.objectId = String(object.id);
      name.value = object.name;
      function sendRename() {
        postCommand({
          type: "renameObject",
          payload: {
            id: object.id,
            name: name.value
          }
        });
      }
      name.addEventListener("blur", sendRename);
      name.addEventListener("keydown", function (event) {
        if (event.key === "Enter") {
          event.preventDefault();
          sendRename();
        }
      });
      nameLabel.appendChild(name);

      const positionLabel = document.createElement("label");
      positionLabel.textContent = "Position";
      const position = document.createElement("input");
      position.readOnly = true;
      position.value = object.position.map(function (value) {
        return Number(value).toFixed(2);
      }).join(", ");
      positionLabel.appendChild(position);

      const meta = document.createElement("div");
      meta.className = "meta";
      meta.innerHTML = "<span>ID</span><span>" + object.id + "</span><span>Type</span><span>" + object.type + "</span>";

      row.appendChild(nameLabel);
      row.appendChild(positionLabel);
      row.appendChild(meta);
      form.appendChild(row);
    });

    if (focused) {
      const selector = 'input[data-role="object-name"][data-object-id="' + focused.id + '"]';
      const input = form.querySelector(selector);
      if (input) {
        input.focus();
        input.setSelectionRange(focused.selectionStart, focused.selectionEnd);
      }
    }
  }

  window.vsgCefSimple = {
    receiveObjects: function (nextObjects) {
      const incoming = Array.isArray(nextObjects) ? nextObjects : [];
      const incomingJson = JSON.stringify(incoming);
      if (incomingJson === lastObjectsJson) return;
      lastObjectsJson = incomingJson;
      objects = incoming;
      render();
    }
  };

  render();
}());
