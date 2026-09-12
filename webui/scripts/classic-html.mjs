import { readFile, writeFile } from "node:fs/promises";

for (const file of ["property-editor.html", "outliner.html", "render-status.html", "robot-configurator.html"]) {
  const htmlPath = new URL(`../dist/${file}`, import.meta.url);
  const html = await readFile(htmlPath, "utf8");
  await writeFile(htmlPath, html
    .replaceAll(' type="module"', "")
    .replaceAll(' crossorigin', "")
    .replaceAll('<script src=', '<script defer src='));
}
