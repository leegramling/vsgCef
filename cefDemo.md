# CEF Demo Input and HTML-to-C++ Flow

This document describes the input and command path used by `vsgCefPanelDemo`.

The demo has two separate directions of communication:

1. VSG sends mouse and keyboard input into the windowless CEF browser.
2. JavaScript sends application commands back to C++ through CEF IPC.
3. C++ sends application state back to JavaScript by executing JavaScript in
   the browser renderer.

The CEF page is not a native window. It is rendered off-screen into a BGRA
pixel buffer, which `UIPanel` displays on a VSG textured quad.

## 1. HTML button to C++

The button is defined in:

```text
cef_demo_ui/hello.html
```

Its click handler is in:

```text
cef_demo_ui/hello.js
```

The JavaScript button handler sends this command:

```javascript
window.app.action("addCubes", { count: 3 });
```

`window.app.action()` is implemented in:

```text
cef_demo_ui/bridge.js
```

It serializes the command as JSON and calls CEF's injected `cefQuery()` API:

```javascript
window.cefQuery({
  request: JSON.stringify({
    action: "addCubes",
    args: { count: 3 }
  })
});
```

CEF's message router transports this request from the renderer process to the
browser process. `CefUi` receives it through its browser-side message-router
handler in:

```text
src/vsgcef/CefUi.cpp
```

The request is parsed into a `vsgcef::CefUiCommand`:

```cpp
command.type      // "addCubes"
command.argsJson  // {"count":3}
```

The application installs the command callback in:

```text
src/vsgCefPanelDemo/main.cpp
```

The callback parses the JSON and places a small command on a thread-safe VSG
queue:

```cpp
state->commands->add(AddCubes{count});
```

The CEF callback does not directly edit the VSG scene. This is important because
CEF callbacks and VSG traversal/update work may occur in different contexts.

## 2. C++ to JavaScript state updates

The reverse direction uses `CefUi::executeJavaScript()`. The C++ application
executes a small script in the `hello` browser surface:

```cpp
state.cefUi->executeJavaScript(
    "hello",
    "if (window.__vsgCef) "
    "window.__vsgCef.receiveState('selectedObject',{name:'cube1'});");
```

`bridge.js` exposes `window.__vsgCef.receiveState()`. It looks up the matching
subscription and invokes the JavaScript callback registered by the page:

```javascript
window.app.subscribe("selectedObject", function (state) {
  document.getElementById("selected").textContent =
    "Selected Object: " + (state.name || "none");
});
```

The selected-object path in `vsgCefPanelDemo` is:

```text
VSG mouse click
  -> LineSegmentIntersector ray
  -> nearest cube transform
  -> update cube vertex colors
  -> CefUi::executeJavaScript()
  -> window.__vsgCef.receiveState()
  -> hello.js subscription
  -> HTML selected-object label
```

When a cube is selected, its color changes to yellow and the panel displays,
for example, `Selected Object: cube1`. Clicking a cube is handled by the native
VSG selection visitor; clicks inside the CEF panel are excluded so they remain
available to the HTML controls.

## 3. Property editor panel

The demo also contains a second CEF surface named `properties`. It is rendered
by a second `UIPanel` in the same fixed overlay view:

```text
hello surface       -> add-cubes controls and selected-object label
properties surface  -> selected cube position editor
```

The property editor displays `Tx`, `Ty`, and `Tz` for the selected cube. Each
value can be changed in either of two ways:

- Type a number into the corresponding HTML input.
- Drag the coordinate label horizontally. The label converts pointer movement
  into a position delta and sends the updated values to C++.

Both controls send the same application action:

```javascript
window.app.action("setCubePosition", {
  id: selectedId,
  tx: values.tx,
  ty: values.ty,
  tz: values.tz
});
```

C++ validates the cube id, queues a `SetCubePosition` scene command, and the VSG
update loop applies the new `MatrixTransform` position. The updated position is
then published back to the `properties` surface, so the HTML form remains a
view of the C++ model rather than becoming a second source of truth.

## 4. Endpoint-style application API

The current example uses named `app.action()` calls because that is the
smallest plain-JavaScript bridge. A larger application can put a typed endpoint
registry behind the same transport:

```cpp
panel.command("cubes.add", [](const Json& args, EndpointContext& context) {
    const auto count = args.requiredInt("count");
    if (count < 1 || count > 1000)
        return EndpointResult::error("Count must be between 1 and 1000.");

    context.queue(AddCubes{count});
    return EndpointResult::success();
});
```

The web code would call the endpoint by name:

```javascript
await app.call("cubes.add", { count: 5 });
```

This keeps C++ function registration, argument validation, structured errors,
and command queuing in C++, while the browser only knows endpoint names and
JSON arguments. The transport can use CEF's existing message router; the
endpoint layer would hide `cefQuery()` and direct JavaScript strings from
application code.

For a retained UI, use endpoint calls for commands and queries, and state
subscriptions for updates:

```text
C++ endpoint command/query  <- app.call(name, args)
C++ published state         -> app.subscribe(name, callback)
```

This is compatible with plain HTML/JavaScript, Vue, React, and Svelte because
none of those frameworks need to know how the C++ endpoint is implemented.

## 5. C++ queue to VSG scene

The main VSG loop consumes queued commands:

```cpp
for (const auto& command : state->commands->take_all())
    addCubes(*state, command.count);
```

`addCubes()` then:

1. Creates a cube transform.
2. Positions it using a grid offset so cubes do not overlap.
3. Compiles its VSG resources when necessary.
4. Adds it to the VSG scene group.

The command path is therefore:

```text
HTML button
  -> hello.js
  -> bridge.js
  -> window.cefQuery()
  -> CEF renderer/browser IPC
  -> CefMessageRouter
  -> CefUiCommand
  -> C++ command handler
  -> ThreadSafeQueue<AddCubes>
  -> VSG main loop
  -> addCubes()
  -> VSG scene graph
```

## 6. VSG mouse input to CEF

The VSG-native panel is implemented in:

```text
src/vsgcef/UIPanel.h
src/vsgcef/UIPanel.cpp
```

`UIPanelInputHandler` is registered with the VSG viewer. It receives VSG
events such as:

```cpp
vsg::MoveEvent
vsg::ButtonPressEvent
vsg::ButtonReleaseEvent
vsg::KeyPressEvent
vsg::KeyReleaseEvent
```

The handler projects the panel's world-space corners into window coordinates and
checks whether the mouse event is inside the panel. It then converts the window
position to CEF browser pixels:

```text
VSG window coordinates
  -> panel hit test
  -> normalized panel coordinates
  -> CEF pixel coordinates
```

For example, a left click becomes:

```cpp
cefUi->sendMouseClick(
    "hello",
    browserX,
    browserY,
    modifiers,
    vsgcef::CefMouseButton::Left,
    false,
    1);
```

The corresponding mouse-release event is sent separately with `mouseUp = true`.

`CefUi` obtains the browser host for the named surface and calls CEF's
windowless input API:

```cpp
browser->GetHost()->SendMouseClickEvent(...);
```

The browser host is also focused when the mouse button goes down, which allows
keyboard input to reach an HTML `<input>` element.

Keyboard events are converted from VSG key symbols to CEF/Windows virtual-key
codes. For example:

```text
VSG KEY_BackSpace -> CEF virtual key 0x08
VSG KEY_Return    -> CEF virtual key 0x0D
VSG KEY_Delete    -> CEF virtual key 0x2E
```

Printable characters are sent with both a raw key event and a CEF character
event so that HTML text fields can edit their contents.

## 7. CEF pixels to the VSG quad

CEF calls the render handler's `OnPaint()` method with BGRA pixels. The render
handler stores those pixels in a `CefSurfaceFrame`.

Each frame, `UIPanel::update()` copies the latest pixels into a dynamic VSG
image:

```text
CEF OnPaint()
  -> BGRA pixel buffer
  -> dynamic ubvec4Array2D
  -> Vulkan image upload
  -> custom two-triangle VSG panel
```

The panel shaders are compiled offline by CMake:

```text
shaders/ui_panel.vert
shaders/ui_panel.frag
```

The panel therefore does not require ImGui or runtime GLSLang shader
compilation.

## 8. Header dragging

The top portion of the panel is treated as a native drag header by
`UIPanelInputHandler`. A left drag in that region changes the panel's
`MatrixTransform` position. While dragging, mouse events are consumed by the
panel mover instead of being forwarded to the HTML page.

Once the button is released, normal mouse forwarding resumes. The panel also
automatically clears its drag state if a move event arrives without the left
button mask, which protects against a missed release event.

## 9. Menu, toolbar, and cube labels

The native panel demo also includes a CEF-rendered top toolbar:

```text
toolbar.html
  -> File menu button
  -> colored icon placeholders
  -> HTML popup containing File -> Exit
```

The toolbar and its popup are intentionally one CEF surface. The popup is an
HTML element inside that surface, so opening it does not create another CEF
browser or another Vulkan texture. The toolbar uses a non-draggable
`UIPanelInputHandler` so its buttons receive mouse input normally.

Cube labels use the same approach as the earlier `vsgCefDemo`: an ImGui
foreground draw list projects each cube position through the world camera and
draws `Cube 1`, `Cube 2`, and so on. The labels therefore follow camera rotation
and cube movement without requiring a CEF browser surface for every label.

Several CEF textures are supported. Each named surface has its own browser,
off-screen paint buffer, Vulkan image upload, and input mapping. The overhead is
mostly proportional to the surface pixel area and how often its page repaints;
the number of small static surfaces is usually reasonable, but continuously
animated or full-window surfaces can become expensive because every repaint
copies and uploads BGRA pixels. A practical design is to use CEF surfaces for
interactive web panels, combine related toolbar/popup content into one surface,
and use native VSG or ImGui drawing for lightweight labels and indicators.
