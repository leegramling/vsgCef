(function () {
  const list = document.getElementById("objectList");
  const status = document.getElementById("status");
  let objects = [];
  let lastObjectsJson = "";
  function render() {
    list.replaceChildren();
    status.textContent = objects.length + " objects from C++";

    objects.forEach(function (object) {
      const row = document.createElement("button");
      row.type = "button";
      row.className = "object-row";
      if (object.selected) row.classList.add("selected");
      row.addEventListener("click", function () {
        app.action("object.select", { id: object.id });
      });

      row.textContent = object.name;
      list.appendChild(row);
    });
  }

  app.subscribe("objects", function (nextObjects) {
    const incoming = Array.isArray(nextObjects) ? nextObjects : [];
    const incomingJson = JSON.stringify(incoming);
    if (incomingJson === lastObjectsJson) return;
    lastObjectsJson = incomingJson;
    objects = incoming;
    render();
  });

  render();
  app.ready();
}());
