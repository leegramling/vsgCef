(function () {
  const form = document.getElementById("cubeForm");
  const count = document.getElementById("count");
  const status = document.getElementById("status");
  form.addEventListener("submit", function (event) {
    event.preventDefault();
    window.app.action("addCubes", { count: Number(count.value) });
  });
  window.app.subscribe("form", function (state) {
    status.textContent = "Cubes in scene: " + state.cubeCount;
  });
  window.app.ready();
}());
