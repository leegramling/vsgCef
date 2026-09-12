(function () {
  const summary = document.getElementById("summary");
  const details = document.getElementById("details");
  let selected = null;

  function field(label, value, editable, onChange, draggable) {
    const wrapper = document.createElement("label");
    wrapper.className = "property-field";
    const labelText = document.createElement("span");
    labelText.textContent = label;
    if (draggable) labelText.className = "drag-label";
    wrapper.appendChild(labelText);

    const input = document.createElement("input");
    input.value = value;
    input.readOnly = !editable;
    if (editable) {
      const send = function () { onChange(input.value); };
      input.addEventListener("blur", send);
      input.addEventListener("keydown", function (event) {
        if (event.key === "Enter") {
          event.preventDefault();
          send();
        }
      });
    }
    if (draggable) {
      labelText.addEventListener("pointerdown", function (event) {
        if (event.button !== 0) return;
        event.preventDefault();
        const startX = event.clientX;
        const startValue = Number(input.value) || 0;
        labelText.setPointerCapture(event.pointerId);
        function move(moveEvent) {
          const value = startValue + (moveEvent.clientX - startX) * 0.02;
          input.value = value.toFixed(3);
        }
        function end() {
          onChange(Number(input.value));
          labelText.removeEventListener("pointermove", move);
          labelText.removeEventListener("pointerup", end);
          labelText.removeEventListener("pointercancel", end);
        }
        labelText.addEventListener("pointermove", move);
        labelText.addEventListener("pointerup", end);
        labelText.addEventListener("pointercancel", end);
      });
    }
    wrapper.appendChild(input);
    return wrapper;
  }

  function setTransform(axis, value) {
    const numeric = Number(value);
    if (!selected || !Number.isFinite(numeric)) return;
    app.action("object.setTransform", { id: selected.id, axis: axis, value: numeric });
  }

  function render() {
    details.replaceChildren();
    if (!selected) {
      summary.textContent = "Select an object in the Outliner or scene";
      return;
    }

    summary.textContent = "Editing selected object";
    const position = selected.position || [0, 0, 0];
    const rotation = selected.rotation || [0, 0, 0];
    details.appendChild(field("Name", selected.name, true, function (name) {
      app.action("object.rename", { id: selected.id, name: name });
    }, false));
    details.appendChild(field("ID", selected.id, false, null, false));
    details.appendChild(field("Type", selected.type, false, null, false));
    [["Tx", "tx", position[0]], ["Ty", "ty", position[1]], ["Tz", "tz", position[2]],
     ["Rx", "rx", rotation[0]], ["Ry", "ry", rotation[1]], ["Rz", "rz", rotation[2]]].forEach(function (item) {
      details.appendChild(field(item[0], Number(item[2]).toFixed(3), true, function (value) {
        setTransform(item[1], value);
      }, true));
    });
  }

  app.subscribe("selection", function (object) {
    selected = object;
    render();
  });

  app.ready();
}());
