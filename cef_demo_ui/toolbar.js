(function () {
  const fileButton = document.getElementById("file");
  const popup = document.getElementById("file-popup");
  fileButton.addEventListener("click", function () {
    popup.hidden = !popup.hidden;
  });
  document.getElementById("exit").addEventListener("click", function () {
    window.app.action("exit", {});
    popup.hidden = true;
  });
  window.app.ready();
}());
