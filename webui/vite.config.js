import { svelte } from "@sveltejs/vite-plugin-svelte";
import { defineConfig } from "vite";
import { resolve } from "node:path";

const entry = process.env.VSGCEF_ENTRY || "property-editor";

export default defineConfig({
  base: "./",
  root: resolve("src"),
  plugins: [svelte()],
  build: {
    outDir: resolve("dist"),
    emptyOutDir: entry === "property-editor",
    rollupOptions: {
      input: resolve(`src/${entry}.html`),
      output: {
        format: "iife",
        inlineDynamicImports: true
      }
    }
  }
});
