(function () {
  const summary = document.getElementById("summary");
  const bridge = document.getElementById("bridge");
  const count = document.getElementById("count");
  const sceneFps = document.getElementById("sceneFps");
  const cefFps = document.getElementById("cefFps");
  const cefPaintFps = document.getElementById("cefPaintFps");
  const selected = document.getElementById("selected");
  app.subscribe("renderStatus", function (status) {
    sceneFps.value = Number(status.sceneFps || 0).toFixed(1);
    cefFps.value = Number(status.cefFps || 0).toFixed(1);
    cefPaintFps.value = Number(status.cefPaintFps || 0).toFixed(1);
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
