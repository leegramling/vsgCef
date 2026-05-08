# vsgthreading Architecture

`vsgthreading` demonstrates a VSG-idiomatic threaded app:

- VSG viewer, event handlers, ImGui recording, and attached scene graph mutation stay on the main thread.
- Simulation runs as `vsg::Operation` work on `vsg::OperationThreads`.
- Worker operations publish immutable frame diffs.
- Main-thread update operations apply those diffs to VSG objects during `viewer->update()`.

## Sequence

```mermaid
sequenceDiagram
    autonumber
    participant Main as Main Thread
    participant Viewer as vsg::Viewer
    participant Input as FloorClickHandler / StatsUi
    participant AppData as AppData
    participant Workers as vsg::OperationThreads
    participant SimOp as SimulationStepOperation
    participant Sim as Simulator
    participant PubOp as PublishFrameOperation
    participant Scene as SceneObject Registry
    participant ImGui as StatsGuiCommand

    Main->>Viewer: advanceToNextFrame()
    Main->>Viewer: handleEvents()
    Viewer->>Input: apply(ButtonPressEvent)
    Input->>AppData: publishEvent(AddCubeEvent)
    Viewer->>Input: record ImGui controls
    Input->>AppData: publishEvent(SetPausedEvent / SpawnBurstEvent / ClearObjectsEvent)

    Workers->>SimOp: run()
    SimOp->>AppData: takeSimulatorEvents()
    SimOp->>Sim: step(dt, events, counters)
    Sim-->>SimOp: shared_ptr<const FrameData>
    SimOp->>AppData: publishFrame(frame)
    SimOp->>Viewer: addUpdateOperation(PublishFrameOperation)
    SimOp->>Workers: add(next SimulationStepOperation)

    Main->>Viewer: update()
    Viewer->>PubOp: run()
    PubOp->>AppData: takePendingFrame()
    PubOp->>Scene: create/update/remove SceneObject nodes
    PubOp->>Viewer: compileManager->compile(new node)
    PubOp->>Viewer: updateViewer(viewer, compileResult)
    PubOp->>Scene: attach compiled node to dynamic group

    Main->>Viewer: recordAndSubmit()
    Viewer->>ImGui: record(commandBuffer)
    ImGui->>Scene: read RenderState.currentFrame
    ImGui->>AppData: publishEvent(control changes)
    Main->>Viewer: present()
```

## Classes

```mermaid
classDiagram
    class VsgThreadingApp {
        +run(argc, argv) int
    }

    class AppData {
        -mutex mutex_
        -vector~AppEvent~ simulatorEvents_
        -shared_ptr~const FrameData~ latestFrame_
        -shared_ptr~const FrameData~ pendingFrame_
        +publishEvent(AppEvent)
        +takeSimulatorEvents() vector~AppEvent~
        +publishFrame(shared_ptr~const FrameData~)
        +latestFrame() shared_ptr~const FrameData~
        +takePendingFrame() shared_ptr~const FrameData~
    }

    class Simulator {
        -unordered_map~uint64_t,ObjectState~ objects_
        +step(dt, events, pendingEventCount, workerBacklog) shared_ptr~const FrameData~
    }

    class SimulationStepOperation {
        +run()
        -shared_ptr~AppData~ appData_
        -shared_ptr~Simulator~ simulator_
        -observer_ptr~vsg::Viewer~ viewer_
        -ref_ptr~vsg::OperationThreads~ workers_
    }

    class PublishFrameOperation {
        +run()
        -shared_ptr~AppData~ appData_
        -observer_ptr~vsg::Viewer~ viewer_
        -ref_ptr~RenderState~ renderState_
    }

    class RenderState {
        +ref_ptr~vsg::Group~ dynamicGroup
        +ref_ptr~vsg::Node~ cubePrototype
        +ref_ptr~vsg::Node~ spherePrototype
        +unordered_map~uint64_t,ref_ptr~SceneObject~~ objects
        +shared_ptr~const FrameData~ currentFrame
        +double renderFps
    }

    class SceneObject {
        -uint64_t id_
        -ObjectType type_
        -ref_ptr~vsg::MatrixTransform~ transform_
        -ref_ptr~vsg::Node~ prototype_
        +init(parent)
        +update(ObjectState)
        +node() ref_ptr~vsg::MatrixTransform~
    }

    class StatsGuiCommand {
        +record(commandBuffer)
        -shared_ptr~AppData~ appData_
        -ref_ptr~RenderState~ renderState_
        -shared_ptr~StatsUi~ ui_
    }

    class StatsUi {
        +init()
        +render(FrameData)
    }

    class FloorClickHandler {
        +apply(ButtonPressEvent)
    }

    class FrameData {
        +simulationFrame
        +simulationTimeSeconds
        +renderFps
        +createdObjects
        +updatedObjects
        +removedObjectIds
    }

    class AppEvent {
        <<variant>>
        AddCubeEvent
        SpawnBurstEvent
        SetPausedEvent
        SetSpawnRateEvent
        ClearObjectsEvent
    }

    VsgThreadingApp --> AppData
    VsgThreadingApp --> RenderState
    VsgThreadingApp --> SimulationStepOperation
    VsgThreadingApp --> FloorClickHandler
    SimulationStepOperation --> AppData
    SimulationStepOperation --> Simulator
    SimulationStepOperation --> PublishFrameOperation
    Simulator --> FrameData
    AppData --> AppEvent
    AppData --> FrameData
    PublishFrameOperation --> AppData
    PublishFrameOperation --> RenderState
    RenderState --> SceneObject
    SceneObject --> FrameData
    StatsGuiCommand --> StatsUi
    StatsGuiCommand --> RenderState
    StatsUi --> AppData
    FloorClickHandler --> AppData
```

## Operation Creation

The app creates one `vsg::OperationThreads` instance:

```cpp
auto workers = vsg::OperationThreads::create(numWorkerThreads, viewer->status);
```

The first simulation operation is queued after `viewer->compile(...)`:

```cpp
workers->add(SimulationStepOperation::create(
    appData,
    simulator,
    vsg::observer_ptr<vsg::Viewer>(viewer),
    workers,
    renderState,
    Clock::now()));
```

`SimulationStepOperation` is a `vsg::Operation` subclass. Its `run()` method executes on a VSG worker thread. It:

- sleeps until the next fixed simulation tick,
- consumes pending app events from `AppData`,
- advances `Simulator::step(...)`,
- publishes the resulting immutable `FrameData`,
- schedules `PublishFrameOperation` with `viewer->addUpdateOperation(...)`,
- queues the next `SimulationStepOperation` back onto `OperationThreads`.

That last step keeps the simulator as VSG operation work instead of using an app-owned raw thread loop.

## Data Into The Simulator

Input and UI do not call simulator methods directly.

Main-thread producers publish `AppEvent` values:

- `FloorClickHandler` publishes `AddCubeEvent`.
- `StatsUi` publishes `SetPausedEvent`, `SetSpawnRateEvent`, `SpawnBurstEvent`, and `ClearObjectsEvent`.

`AppData::publishEvent(...)` stores those events behind a short mutex lock. The worker thread later calls `AppData::takeSimulatorEvents()`, which swaps the queue into a local vector. Physics never runs while holding the `AppData` mutex.

The simulator owns all mutable simulation state:

- object map,
- velocities,
- age/lifetime,
- spawn accumulator,
- pause/spawn-rate settings.

The simulator returns a `std::shared_ptr<const FrameData>`. After publication, that frame is treated as immutable.

## Data Back To Scene Objects

`FrameData` is a diff, not a full scene replacement:

- `createdObjects` contains new simulator ids that need VSG nodes.
- `updatedObjects` contains active object transforms/state updates.
- `removedObjectIds` contains ids that should be detached from the VSG group.

`PublishFrameOperation` runs from `viewer->update()` on the main thread. This is the point where attached scene graph mutation is allowed.

For new objects, `PublishFrameOperation`:

1. Creates a `SceneObject` with a shared cube or sphere prototype.
2. Updates its `vsg::MatrixTransform`.
3. Compiles the new node with `viewer->compileManager->compile(object->node())`.
4. Calls `updateViewer(*viewer, compileResult)`.
5. Attaches the compiled node to `RenderState.dynamicGroup`.
6. Stores it in `RenderState.objects` by simulator id.

For existing objects, it calls:

```cpp
sceneObject->update(objectState);
```

That updates only the `vsg::MatrixTransform` matrix. The simulator never touches VSG nodes.

For removed objects, it removes the node from the dynamic group and erases the registry entry.

## Data To ImGui

`StatsGuiCommand` is a `vsg::Command` passed to:

```cpp
vsgImGui::RenderImGui::create(window, StatsGuiCommand::create(appData, renderState));
```

During record traversal, `StatsGuiCommand::record(...)` reads `RenderState.currentFrame`, copies it to a local display frame, adds the current render FPS, and calls:

```cpp
ui_->render(displayFrame);
```

`StatsUi::render(...)` displays frame stats and publishes UI control changes as `AppEvent` messages. Those UI events follow the same path as floor-click events: `StatsUi -> AppData -> SimulationStepOperation -> Simulator`.

## Synchronization Rules

VSG handles synchronization for its own operation queues:

- `vsg::OperationQueue` is thread-safe.
- `vsg::OperationThreads` consumes `vsg::Operation` objects from that queue.
- `vsg::UpdateOperations` accepts operations from other threads and runs them during `viewer->update()`.

App-owned data still needs explicit synchronization:

- `AppData` uses a mutex for event queues and frame handoff.
- Locks are short: push, swap, or copy a shared pointer.
- Mutable simulator state is confined to the simulation operation thread.
- Mutable VSG scene state is confined to the main/update thread.
- ImGui reads the main-thread published `RenderState.currentFrame`.

The important rule is: VSG operations schedule work safely, but they do not automatically make arbitrary app objects thread-safe.
