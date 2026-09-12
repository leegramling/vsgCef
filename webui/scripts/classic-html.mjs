import { readFile, writeFile } from "node:fs/promises";

const htmlPath = new URL("../dist/property-editor.html", import.meta.url);
const html = await readFile(htmlPath, "utf8");
await writeFile(htmlPath, html.replace(' type="module"', "").replace(' crossorigin', "").replace('<script src=', '<script defer src='));
