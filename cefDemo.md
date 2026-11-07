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

## 3. C++ queue to VSG scene

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

## 4. VSG mouse input to CEF

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

## 5. CEF pixels to the VSG quad

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

## 6. Header dragging

The top portion of the panel is treated as a native drag header by
`UIPanelInputHandler`. A left drag in that region changes the panel's
`MatrixTransform` position. While dragging, mouse events are consumed by the
panel mover instead of being forwarded to the HTML page.

Once the button is released, normal mouse forwarding resumes. The panel also
automatically clears its drag state if a move event arrives without the left
button mask, which protects against a missed release event.
