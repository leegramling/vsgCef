(function () {
  let selectedId = -1;
  const values = { tx: 0, ty: 0, tz: 0 };
  const fields = { tx: document.getElementById("tx"), ty: document.getElementById("ty"), tz: document.getElementById("tz") };

  function publishPosition() {
    if (selectedId < 0) return;
    window.app.action("setCubePosition", {
      id: selectedId, tx: values.tx, ty: values.ty, tz: values.tz
    });
    document.getElementById("message").textContent = "Position sent to C++";
  }

  function updateField(axis, value) {
    values[axis] = Number.isFinite(Number(value)) ? Number(value) : 0;
    fields[axis].value = values[axis].toFixed(3);
  }

  window.app.subscribe("objectProperties", function (state) {
    selectedId = Number(state.id);
    document.getElementById("selected").textContent = "Selected Object: " + (state.name || "none");
    ["tx", "ty", "tz"].forEach(function (axis) {
      updateField(axis, state[axis]);
      fields[axis].disabled = selectedId < 0;
    });
    document.getElementById("message").textContent = selectedId < 0 ? "Select a cube to edit it." : "Ready";
  });

  ["tx", "ty", "tz"].forEach(function (axis) {
    fields[axis].addEventListener("input", function () {
      updateField(axis, fields[axis].value);
      publishPosition();
    });

    const label = document.querySelector("label[data-axis='" + axis + "']");
    label.addEventListener("mousedown", function (event) {
      if (selectedId < 0) return;
      event.preventDefault();
      dragAxis = axis;
      dragStartX = event.clientX;
      dragStartValue = values[axis];
    });
  });

  let dragAxis = null;
  let dragStartX = 0;
  let dragStartValue = 0;
  document.addEventListener("mousemove", function (event) {
    if (!dragAxis || selectedId < 0) return;
    updateField(dragAxis, dragStartValue + (event.clientX - dragStartX) * 0.02);
    publishPosition();
  });
  document.addEventListener("mouseup", function () { dragAxis = null; });

  window.app.ready();
}());
