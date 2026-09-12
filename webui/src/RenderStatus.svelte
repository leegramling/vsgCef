<script>
  import { onMount } from "svelte";
  import { app } from "./bridge.js";

  let status = null;
  let objectCount = 0;
  let error = "";

  $: connected = status !== null;
  $: summary = error || (connected ? "Render status connected to C++" : "Waiting for C++ state");
  $: fields = [
    ["Scene FPS", formatNumber(status?.sceneFps)],
    ["CEF FPS", formatNumber(status?.cefFps)],
    ["CEF Paint FPS", formatNumber(status?.cefPaintFps)],
    ["Selected object", status?.selectedObject || "None"],
    ["CEF bridge", connected ? "connected" : "connecting"],
    ["Scene objects", String(status?.objectCount ?? objectCount)]
  ];

  onMount(() => {
    const unsubscribeStatus = app.subscribe("renderStatus", (value) => {
      status = value && typeof value === "object" ? value : null;
    });
    const unsubscribeObjects = app.subscribe("objects", (value) => {
      objectCount = Array.isArray(value) ? value.length : 0;
    });
    const onError = (event) => { error = event.detail; };
    window.addEventListener("vsgcef-error", onError);
    app.ready();
    return () => {
      unsubscribeStatus();
      unsubscribeObjects();
      window.removeEventListener("vsgcef-error", onError);
    };
  });

  function formatNumber(value) {
    return Number(value || 0).toFixed(1);
  }
</script>

<main>
  <header>
    <div>
      <h1>Render Status</h1>
      <p>{summary}</p>
    </div>
    <div class="header-badges">
      <span class="framework-badge">Svelte</span>
      <span class:connected class="status-dot" aria-label={connected ? "Connected" : "Connecting"}></span>
    </div>
  </header>

  <section class="status-list" aria-label="Render statistics">
    {#each fields as field}
      <label>
        <span>{field[0]}</span>
        <input value={field[1]} readonly />
      </label>
    {/each}
  </section>
</main>

<style>
  :global(*) { box-sizing: border-box; }
  :global(body) { margin: 0; background: #171b1d; color: #e9eef0; font: 14px/1.4 system-ui, sans-serif; }
  main { min-height: 100vh; padding: 14px; }
  header { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; margin-bottom: 14px; padding: 9px 11px; border-left: 4px solid #a87bd8; background: #2d2438; }
  h1 { margin: 0 0 4px; font-size: 20px; }
  p { margin: 0; color: #b7a9c5; }
  .header-badges { display: flex; align-items: center; gap: 9px; }
  .framework-badge { padding: 3px 7px; border: 1px solid #66507a; border-radius: 999px; color: #d6b8ef; font-size: 11px; }
  .status-dot { width: 10px; height: 10px; margin-top: 4px; border-radius: 50%; background: #8b6c9e; box-shadow: 0 0 0 3px #443650; }
  .status-dot.connected { background: #72c58a; box-shadow: 0 0 0 3px #31523b; }
  .status-list { display: grid; gap: 2px; }
  label { display: grid; grid-template-columns: 130px minmax(0, 1fr); align-items: center; min-height: 34px; padding: 3px 0; border-bottom: 1px solid #2b3336; color: #b6c3c7; font-size: 12px; }
  input { width: 100%; border: 1px solid #46545a; border-radius: 4px; padding: 8px 9px; color: #9fb0b6; background: #1b2022; font: inherit; }
</style>
