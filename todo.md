# vsgCef HTML UI Migration TODO

Goal: build a simple, repeatable path for moving app UI from Dear ImGui to HTML/CEF while keeping C++ business logic easy to write and easy to find.

The target developer experience should feel close to the existing UI class pattern:

```cpp
class MyUi
{
public:
    void init(HtmlUi& ui);
    void update(const AppState& state);
};
```

The C++ UI class registers pages, state producers, and callbacks. HTML renders the controls and calls standard JavaScript bridge helpers. Business logic stays in C++ callbacks instead of being embedded in UI markup or JavaScript.

## Current Baseline

- `vsgCefSimple` is the first reference target.
- It renders a VSG floor, cubes, and a sphere.
- It hosts a CEF-backed HTML panel inside an ImGui window.
- C++ sends object state to JavaScript.
- JavaScript sends `renameObject` commands back to C++.
- CEF artifacts are staged in `build/cef`, matching the working `../imguiCef` style.

## Direction Decision

Use Svelte for the first real HTML UI framework.

Reasons:

- Svelte is closer to plain HTML/CSS/JS than React, which should make migration from ImGui panels less noisy.
- Svelte components are concise for form-heavy UI with thousands of controls.
- Two-way local form state is straightforward without a large runtime mental model.
- The compiled output is static HTML/CSS/JS, which is a good fit for local CEF loading.
- Vue would also work, but Svelte has less ceremony for component-local state and small embedded panels.
- React is powerful, but it adds more boilerplate and indirection than this bridge needs.

Keep the bridge framework-agnostic. Svelte should be the recommended frontend, not a hard dependency of the C++ bridge.

## UI Boundary

Keep these in ImGui for now:

- Main menu bar.
- Small toolbars.
- Debug overlays.
- Simple icon buttons for app-global actions.
- Docking/layout experiments while the HTML panel system is still forming.

Move these to HTML/CEF:

- Forms with many fields.
- Tables and lists.
- Inspector panels.
- Property editors.
- Search/filter panels.
- Settings panels.
- Anything with complex validation, text editing, scrolling, selection, or nested controls.

Avoid splitting one conceptual panel between ImGui and HTML. A panel should have one owner unless it is intentionally transitional.

## Multiple Panels

The bridge must support multiple CEF panels from the beginning.

Needed concepts:

```cpp
auto& objects = ui.panel("objects", "objects.html");
auto& inspector = ui.panel("inspector", "inspector.html");
auto& settings = ui.panel("settings", "settings.html");
```

Each panel needs:

- Stable panel id.
- HTML entry file.
- Width and height.
- Visibility.
- Focus state.
- Dirty texture state.
- Input forwarding.
- State subscriptions.
- Optional per-panel command namespace.

Initial layout can stay in ImGui:

- Use ImGui windows as CEF panel hosts.
- Render each CEF surface as an ImGui texture.
- Let ImGui handle moving/resizing/docking while CEF handles rich panel contents.

Later layout options:

- Keep ImGui docking as the native shell.
- Add an HTML-only workspace surface if a whole screen should be web-driven.
- Add saved panel layout once several panels exist.

## C++ Bridge API

Create a small layer above `vsgcef::CefUi`, probably:

```text
src/htmlui/HtmlUi.h
src/htmlui/HtmlUi.cpp
src/htmlui/Json.h
src/htmlui/HtmlPanel.h
```

Desired C++ shape:

```cpp
class ObjectsUi
{
public:
    void init(HtmlUi& ui)
    {
        ui.panel("objects", "objects.html");

        ui.state("objects", [this] {
            return objectsJson();
        });

        ui.action("object.rename", [this](const Json& args) {
            renameObject(args.u64("id"), args.string("name"));
            ui.markDirty("objects");
        });
    }

    void update(HtmlUi& ui)
    {
        ui.publishDirty();
    }
};
```

Required bridge features:

- `panel(id, htmlFile)`.
- `action(name, callback)`.
- `state(name, producer)`.
- `markDirty(stateName)`.
- `publish(stateName)`.
- `publishDirty()`.
- `renderPanelImGui(panelId, deviceId)`.
- `sendInput(panelId, inputEvent)`.

Avoid per-control C++ code. C++ should define domain actions and domain state, not individual buttons and inputs.

## JavaScript Bridge API

Expose one tiny global API to every HTML page:

```js
app.action("object.rename", { id, name });
app.subscribe("objects", objects => render(objects));
app.ready();
```

CEF receives messages in one standard format:

```json
{
  "action": "object.rename",
  "args": {
    "id": 1,
    "name": "Blue Cube"
  }
}
```

C++ sends state in one standard format:

```js
window.__vsgCef.receiveState("objects", data);
```

Add an ACK/ready handshake:

- JavaScript calls `app.ready()` after the page loads.
- C++ does not mark initial state as delivered until the panel is ready.
- C++ can republish dirty state after reconnect/reload.
- C++ logs unknown actions and malformed payloads clearly.

## Svelte Panel Pattern

Recommended Svelte usage:

```svelte
<script>
  import { app } from "./bridge.js";

  let objects = [];

  app.subscribe("objects", value => {
    objects = value;
  });

  function rename(object, name) {
    app.action("object.rename", { id: object.id, name });
  }
</script>

{#each objects as object (object.id)}
  <input value={object.name} on:change={e => rename(object, e.target.value)} />
  <input value={object.position.join(", ")} readonly />
{/each}
```

Important rule:

- Use keyed lists.
- Do not re-create focused inputs on every state tick.
- Prefer local draft state for active text edits.
- Send edits on blur, Enter, or debounce depending on the control.

## DrawList Migration

Some existing ImGui UI uses draw lists. HTML/CEF can handle this, but the replacement depends on what the draw list is doing.

Use normal HTML/CSS for:

- Panels.
- Cards.
- Forms.
- Lists.
- Tables.
- Badges.
- Progress bars.
- Status indicators.
- Tooltips.

Use SVG for:

- Diagrams.
- Node graphs.
- Timelines.
- Scalable vector overlays.
- Connectors and arrows.
- Hit-testable shapes.

Use Canvas for:

- Dense custom drawing.
- Thousands of visual marks.
- Waveforms.
- Heatmaps.
- Mini maps.
- Fast chart-like views.
- Existing ImGui draw-list code that maps naturally to immediate drawing commands.

Use WebGL only when:

- The panel itself needs heavy GPU visualization.
- Canvas/SVG performance is not enough.
- The visualization is separate from the main VSG scene.

Migration advice:

- Do not port draw-list code line-by-line into DOM elements.
- Classify each draw-list widget as form, diagram, chart, or overlay.
- Forms become Svelte components.
- Diagrams become SVG components.
- Dense drawing becomes a Canvas component with a small data API.

## Scaling To Thousands Of Elements

Do not register one C++ callback per UI element.

Use domain actions:

- `object.rename`
- `object.select`
- `object.setVisible`
- `material.setColor`
- `setting.setValue`
- `timeline.scrub`
- `command.execute`

Use grouped state:

- `objects`
- `selection`
- `materials`
- `settings`
- `timeline`
- `diagnostics`

Use stable ids everywhere.

For large data:

- Publish snapshots only when dirty.
- Diff later if snapshots become too expensive.
- Add pagination/windowing for massive tables.
- Let Svelte own transient edit state.
- Use virtual lists for long lists.
- Avoid sending every frame unless the data is truly animated.

## Implementation Plan

1. Stabilize `vsgCefSimple`.
   - Keep it small.
   - Ensure object list appears reliably.
   - Ensure text input focus is stable.
   - Confirm `renameObject` logs in C++.
   - Confirm C++ can send renamed state back to the page.

2. Extract generic panel hosting from `vsgCefSimple`.
   - Move CEF texture upload and ImGui input forwarding into `HtmlPanel`.
   - Keep the sample app free of low-level CEF event code.

3. Add `HtmlUi` action dispatch.
   - Replace hard-coded `renameObject` parsing with generic `{ action, args }`.
   - Keep unknown-action errors visible in stdout and CEF callback failures.
   - Add a tiny JSON helper rather than spreading CEF dictionary parsing everywhere.

4. Add state publishing.
   - Register state producers by name.
   - Track dirty state names.
   - Publish only to panels that are ready.
   - Add initial full-state publish after `app.ready()`.

5. Add multi-panel support.
   - Support at least `objects`, `inspector`, and `settings` panels.
   - Render each panel in an ImGui host window.
   - Track focus per panel.
   - Route mouse and keyboard input to the focused panel only.

6. Introduce Svelte build.
   - Add `ui/` or `webui/` source folder.
   - Build static assets into the executable asset directory.
   - Keep plain HTML sample until Svelte path is proven.
   - Add `bridge.js` as the shared frontend contract.

7. Port one real panel from another app.
   - Pick a form-heavy panel first.
   - Move business logic into named C++ callbacks.
   - Keep menu/toolbar in ImGui.
   - Compare code size and maintainability against the old ImGui version.

8. Port one draw-list panel.
   - Classify it as SVG or Canvas.
   - Build a small Svelte component around that rendering strategy.
   - Keep the C++ side as state plus actions only.

## Open Questions

- Should panels be separate CEF browsers or separate routes inside one browser?
- Should C++ state be JSON strings initially, or should we add a real JSON library?
- Should Svelte assets be built by CMake, or should CMake only copy prebuilt assets?
- Should layout persistence live in ImGui, C++, or HTML?
- Do we need hot reload for HTML/Svelte during development?
- Do we want typed C++ action payloads after the untyped JSON bridge is working?

## Near-Term Done Criteria

The next useful milestone is complete when:

- `vsgCefSimple` shows three objects in HTML.
- Editing a name keeps focus.
- Pressing Enter or leaving the field calls C++.
- C++ prints the rename.
- C++ republishes the renamed object list.
- The scene remains interactive.
- The bridge code is generic enough that adding a second action does not require modifying a central `if command.type == ...` block.
