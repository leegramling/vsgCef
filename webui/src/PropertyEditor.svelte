<script>
  import { onMount } from "svelte";
  import { app } from "./bridge.js";

  let selected = null;
  let error = "";
  let dragState = null;

  $: transforms = selected ? [
    ["Tx", "tx", selected.position?.[0]],
    ["Ty", "ty", selected.position?.[1]],
    ["Tz", "tz", selected.position?.[2]],
    ["Rx", "rx", selected.rotation?.[0]],
    ["Ry", "ry", selected.rotation?.[1]],
    ["Rz", "rz", selected.rotation?.[2]]
  ] : [];

  onMount(() => {
    const unsubscribe = app.subscribe("selection", (value) => {
      selected = value;
    });
    const unsubscribeObjects = app.subscribe("objects", (objects) => {
      const current = Array.isArray(objects) ? objects.find((object) => object.selected) : null;
      if (current) selected = current;
    });
    const onError = (event) => { error = event.detail; };
    window.addEventListener("vsgcef-error", onError);
    app.ready();
    return () => {
      unsubscribe();
      unsubscribeObjects();
      window.removeEventListener("vsgcef-error", onError);
    };
  });

  function rename(event) {
    if (!selected) return;
    app.action("object.rename", { id: selected.id, name: event.currentTarget.value });
  }

  function setTransform(axis, value) {
    if (!selected || !Number.isFinite(value)) return;
    app.action("object.setTransform", { id: selected.id, axis, value });
  }

  function commitTransform(axis, event) {
    setTransform(axis, Number(event.currentTarget.value));
  }

  function beginDrag(axis, event) {
    if (!selected || event.button !== 0) return;
    event.preventDefault();
    dragState = { axis, startX: event.clientX, value: currentValue(axis) };
    window.addEventListener("mousemove", dragTransform);
    window.addEventListener("mouseup", endDrag, { once: true });
  }

  function currentValue(axis) {
    const index = axis[1] === "x" ? 0 : axis[1] === "y" ? 1 : 2;
    return axis[0] === "t" ? Number(selected.position?.[index] || 0) : Number(selected.rotation?.[index] || 0);
  }

  function dragTransform(event) {
    if (!dragState) return;
    const scale = dragState.axis[0] === "r" ? 0.5 : 0.01;
    setTransform(dragState.axis, dragState.value + (event.clientX - dragState.startX) * scale);
  }

  function endDrag() {
    dragState = null;
    window.removeEventListener("mousemove", dragTransform);
  }
</script>

<main>
  <header>
    <div>
      <h1>Property Editor</h1>
      <p>{error || (selected ? "Editing selected object" : "Select an object")}</p>
    </div>
    <span class="framework-badge">Svelte</span>
  </header>

  {#if selected}
    <label>Name <input value={selected.name} on:change={rename} /></label>
    <label>ID <input value={selected.id} readonly /></label>
    <label>Type <input value={selected.type} readonly /></label>
    {#each transforms as channel}
      <div class="property-row">
        <span class="drag-label" role="slider" tabindex="0" aria-label={channel[0]} aria-valuenow={channel[2]} on:mousedown={(event) => beginDrag(channel[1], event)}>{channel[0]}</span>
        <input value={Number(channel[2] || 0).toFixed(3)} on:change={(event) => commitTransform(channel[1], event)} />
      </div>
    {/each}
  {:else}
    <p class="empty">Select an object in the Outliner or scene.</p>
  {/if}
</main>

<style>
  :global(*) { box-sizing: border-box; }
  :global(body) { margin: 0; background: #171b1d; color: #e9eef0; font: 14px/1.4 system-ui, sans-serif; }
  main { padding: 14px; }
  header { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; margin-bottom: 14px; padding: 8px 10px; border-left: 4px solid #d49b4a; background: #352b1f; }
  h1 { margin: 0 0 4px; font-size: 20px; }
  p { margin: 0; color: #9fb0b6; }
  .framework-badge { padding: 3px 7px; border: 1px solid #6e5737; border-radius: 999px; color: #f1c46a; font-size: 11px; }
  label, .property-row { display: grid; grid-template-columns: 96px minmax(0, 1fr); align-items: center; min-height: 30px; padding: 3px 0; border-bottom: 1px solid #2b3336; color: #b6c3c7; }
  .drag-label { cursor: ew-resize; user-select: none; }
  .drag-label:hover { color: #f1c46a; }
  input { width: 100%; border: 1px solid #46545a; border-radius: 4px; padding: 8px 9px; color: #f6f9fa; background: #22282b; font: inherit; }
  input[readonly] { color: #9fb0b6; background: #1b2022; }
  .empty { padding-top: 12px; }
</style>
