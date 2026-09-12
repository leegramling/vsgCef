import { svelte } from "@sveltejs/vite-plugin-svelte";
import { defineConfig } from "vite";
import { resolve } from "node:path";

export default defineConfig({
  base: "./",
  root: resolve("src"),
  plugins: [svelte()],
  build: {
    outDir: resolve("dist"),
    emptyOutDir: true,
    rollupOptions: {
      input: {
        "property-editor": resolve("src/property-editor.html")
      },
      output: {
        format: "iife",
        inlineDynamicImports: true
      }
    }
  }
});
