# CEF JavaScript and C++ Bridge

This document explains how JavaScript running inside the CEF browser panels talks to C++ in `vsgCef`, and how C++ sends frame data back to JavaScript.

The important rule is that JavaScript does not mutate the VSG scene graph directly. JavaScript sends a small command to C++. C++ translates that command into an app event with `publishEvent`. The simulator consumes the event, produces a `FrameData` diff, and the VSG update path applies that diff to the scene on the main thread.

## Current Flow

The CEF UI is rendered from files in `cef_ui/`. The React app in `cef_ui/app.js` sends commands with `window.cefQuery`. CEF routes those commands through `CefMessageRouter`, which calls the C++ `OnQuery` handler. The handler parses the JSON message and passes a typed command into `VsgThreadingApp`.

The scene update path is:

```text
JavaScript control
  -> window.cefQuery(JSON request)
  -> CefMessageRouter renderer/browser IPC
  -> C++ OnQuery(...)
  -> AppData::publishEvent(...)
  -> SimulationStepOperation consumes AppEvent
  -> Simulator::step(...) produces FrameData
  -> PublishFrameOperation applies scene diff during viewer->update()
```

This keeps ownership clean:

- JavaScript owns browser UI state.
- CEF owns browser IPC and offscreen rendering.
- `AppData` owns thread-safe event/frame handoff.
- `Simulator` owns mutable simulation state.
- `PublishFrameOperation` owns VSG scene graph mutation.

## JavaScript To C++

JavaScript uses `cefQuery`, which is injected by the CEF message router renderer-side setup.

The helper in `cef_ui/app.js` builds a JSON message:

```js
function sendToCpp(type, payload = {}, setBridgeStatus) {
  const message = JSON.stringify({ type, payload });
  window.cefQuery({
    request: message,
    onSuccess: () => setBridgeStatus && setBridgeStatus("CEF bridge connected"),
    onFailure: (_code, text) => setBridgeStatus && setBridgeStatus(text || "CEF bridge failure"),
  });
}
```

Current JavaScript command names are:

- `setPaused`
- `setSpawnRate`
- `spawnBurst`
- `clearObjects`
- `mockSettingChanged`

The simulation commands become app events:

```text
setPaused       -> SetPausedEvent
setSpawnRate    -> SetSpawnRateEvent
spawnBurst      -> SpawnBurstEvent
clearObjects    -> ClearObjectsEvent
```

`mockSettingChanged` is currently accepted by the bridge but does not mutate simulation state. It is useful as a pattern for form-only UI messages.

## CEF Message Router

The bridge uses `CefMessageRouter` on both sides of CEF.

On the renderer side, `VsgCefApp` implements `CefRenderProcessHandler`. During `OnContextCreated`, it creates `CefMessageRouterRendererSide` and calls `OnContextCreated(...)`. This is what makes `window.cefQuery` available to JavaScript.

On the browser side, `SurfaceClient` owns `CefMessageRouterBrowserSide`. It forwards `OnProcessMessageReceived(...)` into the router. The router then calls the registered handler's `OnQuery(...)` method.

`ProcessMessage` is the lower-level CEF IPC object that moves messages between the renderer process and browser process. In this app, new UI commands normally should not use `ProcessMessage` directly. Use `cefQuery` in JavaScript and handle it through `CefMessageRouter` / `OnQuery` in C++.

## C++ OnQuery Handler

`OnQuery` receives the raw JSON request from JavaScript. The current handler parses the request with `CefParseJSON`, validates the `type` field, reads known payload fields, and builds a `CefUiCommand`.

Conceptually:

```cpp
bool OnQuery(..., const CefString& request, ..., CefRefPtr<Callback> callback)
{
    CefUiCommand command = commandFromRequest(request, errorMessage);
    if (!errorMessage.empty())
    {
        callback->Failure(400, errorMessage);
        return true;
    }

    if (!commandHandler_(command, errorMessage))
    {
        callback->Failure(400, errorMessage);
        return true;
    }

    callback->Success("ok");
    return true;
}
```

Returning `true` tells `CefMessageRouter` that the query was handled. Every handled query must call either `callback->Success(...)` or `callback->Failure(...)`.

## App Event Publishing

`VsgThreadingApp` installs a `CefUi::CommandHandler` callback when it creates `CefUi`. That callback maps browser commands to app events:

```cpp
if (command.type == "setPaused")
    appData->publishEvent(SetPausedEvent{command.paused});

if (command.type == "setSpawnRate")
    appData->publishEvent(SetSpawnRateEvent{command.objectsPerSecond});

if (command.type == "spawnBurst")
    appData->publishEvent(SpawnBurstEvent{command.count});

if (command.type == "clearObjects")
    appData->publishEvent(ClearObjectsEvent{});
```

`publishEvent` is intentionally the handoff point. The CEF callback should not call simulator methods directly and should not edit VSG nodes. It should translate UI intent into an `AppEvent`.

## C++ To JavaScript

C++ also sends data back into the browser. `StatsUi::publishFrameDataToCef(...)` serializes `FrameData` to JSON and calls:

```cpp
cefUi_->executeJavaScript(vsgcef::CefSurfaceId::Stats, script);
cefUi_->executeJavaScript(vsgcef::CefSurfaceId::Sorting, script);
```

The script calls:

```js
window.vsgCef.receiveFrameData(frame)
```

The React panels register `window.vsgCef.receiveFrameData` with `useEffect`. The stats panel uses it to update FPS, object counts, collision counts, and queue counters. The sorting panel uses it to update live counts.

Use this direction for presentation data that C++ already owns. Use `cefQuery` for JavaScript commands that C++ should act on.

## Adding A New JavaScript Command

To add a future UI command, update both JavaScript and C++.

First, send a named message from JavaScript:

```js
sendToCpp("setWindStrength", { strength: value }, setBridgeStatus);
```

Second, extend the C++ parser in `commandFromRequest(...)` to accept the new `type` and validate its payload.

Third, extend `CefUiCommand` if the payload needs a new typed field.

Fourth, handle the command in the `CefUi::CommandHandler` callback in `VsgThreadingApp`.

Fifth, add a new `AppEvent` variant if the command affects simulation state. The event should be consumed in `Simulator::handleEvent(...)`.

The preferred pattern is:

```text
sendToCpp("newCommand", payload)
  -> commandFromRequest validates payload
  -> CefUiCommand carries typed data
  -> command handler calls publishEvent(NewCommandEvent{...})
  -> Simulator consumes NewCommandEvent
```

## Adding A New C++ To JavaScript Update

For new display-only data, add fields to the JSON generated by `StatsUi::publishFrameDataToCef(...)`, then read those fields in `window.vsgCef.receiveFrameData(frame)`.

Keep the JavaScript receiver tolerant of missing fields:

```js
receiveFrameData(frame) {
  setState((current) => ({
    ...current,
    windStrength: frame.windStrength ?? current.windStrength,
  }));
}
```

That makes browser code resilient while C++ and JavaScript are being developed together.

## Practical Guidance

Use `cefQuery` for button clicks, sliders, toggles, form changes, and other user intent. Keep payloads small and explicit. Avoid sending whole UI state objects unless the simulator actually needs the whole object.

Use `publishEvent` as the C++ boundary for anything that changes simulation behavior. If a command needs to update the scene, create an `AppEvent` and let the existing simulator/frame/update path do the scene work.

Use `executeJavaScript` for C++ owned state that the UI should display. Do not poll C++ from JavaScript for every frame when C++ already has a frame publication point.

Use `ProcessMessage` directly only when `cefQuery` is not a good fit, such as custom renderer/browser process coordination that is not a JavaScript request/response command. For normal UI controls, `cefQuery`, `CefMessageRouter`, and `OnQuery` are the app's standard bridge.

Keep validation on the C++ side. Treat browser messages as untrusted input: check the command name, check payload types, clamp numeric ranges in the simulator or event handler, and return `callback->Failure(...)` when a command is malformed.

