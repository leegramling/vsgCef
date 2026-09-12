# Developing CEF UI With Svelte

This guide describes the current path for adding a Svelte panel to `vsgCefSimple`. The bridge is intentionally small: C++ owns application state and business logic, while Svelte renders state and sends named actions.

## The Flow

The runtime flow is:

1. C++ registers a panel with a stable panel id and an HTML file.
2. CEF loads the built static page from `webui/dist` using `file://`.
3. The page mounts its Svelte component.
4. The component calls `app.ready()` with its panel id.
5. C++ publishes named JSON state with `receiveState(name, data)`.
6. Svelte subscribes to state names and updates its reactive values.
7. User input calls `app.action(name, args)`.
8. C++ validates the action, changes the model, and marks related state dirty.

The browser does not own the scene model. It is a view and input surface for the C++ model.

## Project Layout

The Svelte project is in `webui/`:

```text
webui/
  package.json
  vite.config.js
  src/
    bridge.js
    property-editor.html
    property-editor.js
    PropertyEditor.svelte
  scripts/
    classic-html.mjs
  dist/
    property-editor.html
    assets/...
```

Use lowercase kebab-case for panel and page ids, such as `objects`, `property-editor`, and `render-status`. Component filenames use PascalCase, such as `PropertyEditor.svelte`.

## Create A Svelte Panel

For a new panel, add a page entry and a component.

### 1. Add the HTML entry page

Example: `webui/src/outliner.html`:

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Outliner</title>
  </head>
  <body data-panel="objects">
    <script type="module" src="./outliner.js"></script>
  </body>
</html>
```

The `data-panel` value must match the C++ panel id and the id sent by `app.ready()`.

### 2. Add the JavaScript mount file

Example: `webui/src/outliner.js`:

```js
import { mount } from "svelte";
import Outliner from "./Outliner.svelte";

mount(Outliner, { target: document.body });
```

### 3. Add the Svelte component

The current plain outliner receives an `objects` array and sends `object.select`. The Svelte equivalent is:

```svelte
<script>
  import { onMount } from "svelte";
  import { app } from "./bridge.js";

  let objects = [];

  onMount(() => {
    const unsubscribe = app.subscribe("objects", (value) => {
      objects = Array.isArray(value) ? value : [];
    });
    app.ready();
    return unsubscribe;
  });

  function selectObject(id) {
    app.action("object.select", { id });
  }
</script>

<main>
  <h1>Outliner</h1>
  <p>{objects.length} objects from C++</p>
  {#each objects as object}
    <button class:selected={object.selected}
            type="button"
            on:click={() => selectObject(object.id)}>
      {object.name}
    </button>
  {/each}
</main>
```

The outliner should display only the object name. Editing belongs in the property editor.

## The Bridge

`webui/src/bridge.js` provides three operations:

```js
app.subscribe("objects", callback);
app.action("object.select", { id: 2 });
app.ready();
```

`subscribe()` returns an unsubscribe function. Use it from `onMount()` so the subscription is removed when a panel is destroyed. The bridge caches state received before Svelte subscribes, which avoids a startup race between CEF and component mounting.

The current `ready()` method is configured for `property-editor`. When the outliner is migrated, make this generic by reading the page attribute:

```js
const panelId = document.body.dataset.panel || "";

export const app = {
  // action and subscribe remain unchanged
  ready() {
    action("__vsgCef.ready", { panel: panelId });
  }
};
```

This is the preferred bridge behavior for all future panels.

## Register The Panel In C++

Register the panel after `CefUi` has been attached to `HtmlUi` and before `createBrowsers()`:

```cpp
state->htmlUi->panel(
    "objects",
    "Outliner",
    "cef_objects_input",
    VSGCEF_CEF_UI_DIR "/../webui/dist/outliner.html",
    300,
    800);
```

The arguments are:

1. Panel id used by state routing and `ready`.
2. ImGui window title.
3. The existing CEF input/texture id.
4. The generated static HTML file.
5. Initial width.
6. Initial height.

Render it with the same id:

```cpp
state->htmlUi->renderPanelImGui(
    "objects", state.viewer, deviceID, position, size,
    ImGuiWindowFlags_None);
```

Also publish dirty state for that panel in the frame loop:

```cpp
state.htmlUi->publishDirty("objects");
```

The panel id must be identical in all three places: registration, rendering, and `publishDirty()`.

## Add Or Reuse C++ State

State is registered once with a producer that returns JSON:

```cpp
state->htmlUi->state("objects", [state] {
    return objectsJson(*state);
});
```

The producer should describe current model state. It should not contain UI-specific behavior. The existing `objects` state contains object ids, names, types, positions, rotations, and selection flags.

When the model changes, mark the affected state dirty:

```cpp
state->htmlUi->markDirty("objects");
state->htmlUi->markDirty("selection");
```

The next frame sends those states to ready panels. A panel that has not called `ready()` will not receive JavaScript updates yet.

## Add A C++ Callback

Register business logic under a stable action name:

```cpp
state->htmlUi->action("object.select", [state](
    const htmlui::Json& args, std::string& errorMessage) {
    const uint64_t id = args.u64("id");
    // Validate id, update the C++ model, and mark related state dirty.
    return true;
});
```

The Svelte code only sends the action name and JSON arguments:

```js
app.action("object.select", { id: object.id });
```

Callbacks should validate ids, names, ranges, and enum values in C++. Report failures through `errorMessage`; the bridge exposes failures as the `vsgcef-error` browser event.

## Build And Run

Build the static Svelte output after changing Svelte, JavaScript, HTML, or CSS:

```bash
cd webui
npm run build
```

Then rebuild the native app when C++ changed:

```bash
cd ../build
cmake --build . --target vsgCefSimple
./vsgCefSimple
```

There is no web server in the normal application. CEF loads the generated files directly from `webui/dist`. The build post-processes the property editor output into a classic deferred script because the application uses local `file://` pages.

## Migration Checklist: Plain Outliner To Svelte

Use this checklist when replacing `cef_simple_ui/stats.html`:

- Create `webui/src/outliner.html` with `data-panel="objects"`.
- Create `webui/src/outliner.js` and `webui/src/Outliner.svelte`.
- Make `bridge.js` derive the panel id from `body.dataset.panel`.
- Subscribe to the existing `objects` state.
- Render object names only.
- Send `object.select` on row click.
- Preserve selected-row styling.
- Add the new entry to `vite.config.js`.
- Register the generated `outliner.html` in `main.cpp`.
- Build `webui` and verify `panel ready: objects`.
- Remove the old plain page registration only after the Svelte panel works.

## Troubleshooting

If a panel does not report ready:

- Confirm the panel id matches in the page, C++, and `publishDirty()`.
- Confirm the generated HTML exists at the exact path used by `panel()`.
- Run `npm run build` after changing Svelte source.
- Rebuild `vsgCefSimple` after changing C++.
- Check for `[vsgCef] page load failed` or `[vsgCef] page script error`.
- If the page is blank, inspect whether the generated script is deferred and whether it mounts after `document.body` exists.
- The `UPower` DBus warning is unrelated to panel registration.
