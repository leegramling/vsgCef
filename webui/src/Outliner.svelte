<script>
  import { onMount } from "svelte";
  import { app } from "./bridge.js";

  let objects = [];
  let filter = "";
  let error = "";
  let focusedIndex = 0;

  $: visibleObjects = objects.filter((object) =>
    String(object.name || "").toLowerCase().includes(filter.trim().toLowerCase())
  );

  $: if (focusedIndex >= visibleObjects.length) {
    focusedIndex = Math.max(0, visibleObjects.length - 1);
  }

  onMount(() => {
    const unsubscribe = app.subscribe("objects", (value) => {
      objects = Array.isArray(value) ? value : [];
    });
    const onError = (event) => { error = event.detail; };
    window.addEventListener("vsgcef-error", onError);
    app.ready();
    return () => {
      unsubscribe();
      window.removeEventListener("vsgcef-error", onError);
    };
  });

  function selectObject(object) {
    app.action("object.select", { id: object.id });
  }

  function selectVisible(index) {
    const object = visibleObjects[index];
    if (!object) return;
    focusedIndex = index;
    selectObject(object);
  }

  function handleKeydown(event, index) {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      selectVisible(Math.min(index + 1, visibleObjects.length - 1));
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      selectVisible(Math.max(index - 1, 0));
    } else if (event.key === "Home") {
      event.preventDefault();
      selectVisible(0);
    } else if (event.key === "End") {
      event.preventDefault();
      selectVisible(visibleObjects.length - 1);
    } else if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      selectVisible(index);
    }
  }
</script>

<main>
  <header>
    <div>
      <h1>Outliner</h1>
      <p>{error || `${visibleObjects.length} of ${objects.length} objects`}</p>
    </div>
    <div class="header-badges">
      <span class="framework-badge">Svelte</span>
      <span class="count" aria-label={`${objects.length} objects`}>{objects.length}</span>
    </div>
  </header>

  <label class="search">
    <span>Filter</span>
    <input bind:value={filter} placeholder="Find an object" aria-label="Filter objects" />
  </label>

  <div class="tree" role="tree" aria-label="Scene objects">
    {#if visibleObjects.length}
      {#each visibleObjects as object, index (object.id)}
        <button
          class:selected={object.selected}
          class="tree-row"
          type="button"
          role="treeitem"
          aria-selected={object.selected}
          tabindex={index === focusedIndex ? 0 : -1}
          on:click={() => selectVisible(index)}
          on:keydown={(event) => handleKeydown(event, index)}>
          <span class="disclosure" aria-hidden="true">◆</span>
          <span class="object-name">{object.name}</span>
          <span class="object-type">{object.type}</span>
        </button>
      {/each}
    {:else}
      <p class="empty">{objects.length ? "No matching objects" : "Waiting for C++ state"}</p>
    {/if}
  </div>
</main>

<style>
  :global(*) { box-sizing: border-box; }
  :global(body) { margin: 0; background: #171b1d; color: #e9eef0; font: 14px/1.4 system-ui, sans-serif; }
  main { min-height: 100vh; padding: 14px; }
  header { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; margin-bottom: 14px; padding: 9px 11px; border-left: 4px solid #d49b4a; background: #352b1f; }
  h1 { margin: 0 0 4px; font-size: 20px; }
  p { margin: 0; color: #9fb0b6; }
  .header-badges { display: flex; align-items: center; gap: 6px; }
  .framework-badge, .count { padding: 3px 7px; border: 1px solid #6e5737; border-radius: 999px; color: #f1c46a; font-size: 11px; }
  .count { min-width: 28px; text-align: center; }
  .search { display: block; margin-bottom: 10px; color: #9fb0b6; font-size: 12px; }
  .search span { display: block; margin-bottom: 4px; }
  input { width: 100%; border: 1px solid #46545a; border-radius: 4px; padding: 8px 9px; color: #f6f9fa; background: #22282b; font: inherit; }
  input:focus, .tree-row:focus-visible { outline: 2px solid #d49b4a; outline-offset: 1px; }
  .tree { display: grid; gap: 3px; }
  .tree-row { display: grid; grid-template-columns: 18px minmax(0, 1fr) auto; align-items: center; width: 100%; min-height: 36px; padding: 6px 8px; border: 1px solid transparent; border-radius: 4px; color: #dce5e7; background: transparent; font: inherit; text-align: left; cursor: pointer; }
  .tree-row:hover { background: #242c2f; }
  .tree-row.selected { border-color: #80602f; background: #4a3822; color: #fff3d2; }
  .disclosure { color: #839399; font-size: 8px; transform: rotate(45deg); }
  .selected .disclosure { color: #f1c46a; }
  .object-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .object-type { margin-left: 10px; color: #819096; font-size: 11px; }
  .empty { padding: 14px 4px; }
</style>
