(function () {
  window.app.subscribe("selectedObject", function (state) {
    document.getElementById("selected").textContent = "Selected Object: " + (state.name || "none");
  });
  document.getElementById("add").addEventListener("click", function () {
    window.app.action("addCubes", { count: Number(document.getElementById("count").value) });
    document.getElementById("message").textContent = "Command sent to C++";
  });
  window.app.ready();
}());
