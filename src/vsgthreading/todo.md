# vsgthreading TODO

Goal: define a small VSG app that demonstrates a correct multi-threaded scene/simulation architecture before implementation starts.

Primary reference points:

- Local example: `../vsg_deps/vsgExamples/examples/threading/vsgdynamicload/vsgdynamicload.cpp`
- Local example: `../vsg_deps/vsgExamples/examples/threading/vsgdynamicwindows/vsgdynamicwindows.cpp`
- VSG Doxygen class index: <https://vsg-dev.github.io/vsg-dev.io/ref/VulkanSceneGraph/html/classes.html>
- Existing repo target patterns: `vkvsg`, `vkglobe`, and `vsgdock` in the root `CMakeLists.txt`

## MVP Definition

`vsgCef` is a VSG executable that renders:

- A floor plane.
- Multiple cube objects.
- Multiple sphere objects.
- A Dear ImGui stats panel rendered through `vsgImGui`.

The app exists to illustrate:

- VSG viewer/rendering remains on the main thread.
- Event handlers remain on the main thread.
- Simulation work runs off the main thread through VSG operation infrastructure.
- Scene graph mutations are merged back on the viewer update path using `vsg::Operation`.
- Render objects consume immutable simulation frame snapshots rather than touching simulator state directly.
- Cross-thread app messages move through a small `AppData` handoff object instead of direct simulator/UI references.

## Main Loop Contract

Use the standard VSG loop:

```cpp
while (viewer->advanceToNextFrame())
{
    viewer->handleEvents();
    viewer->update();
    viewer->recordAndSubmit();
    viewer->present();
}
```

Required ownership rule:

- `viewer`, windows, event handlers, command graphs, and live scene graph mutation are owned by the main thread.
- Background operations may create pure CPU data and non-attached object descriptions.
- Background operations must not directly mutate the attached scene graph.
- Any attached graph add/remove/update that requires viewer/compiler coordination is represented as a merge/update operation run from `viewer->update()` or a post-present merge queue.

## Threading Model

Use VSG operation primitives in the same spirit as the threading examples:

- `vsg::OperationThreads` for background work.
- `vsg::Operation` subclasses for explicit work units.
- `viewer->addUpdateOperation(...)` for operations that must run during `viewer->update()`.
- Optional `vsg::OperationQueue` for post-present merges if a graph change needs to occur after presentation, following `vsgdynamicwindows`.

MVP operation types:

- `SimulationStepOperation`
  - Runs on an operation thread.
  - Consumes queued app events.
  - Advances physics by one simulation tick or fixed-step batch.
  - Produces an immutable `FrameData` snapshot.
  - Enqueues a `PublishFrameOperation` or updates a thread-safe latest-frame handoff.

- `PublishFrameOperation`
  - Runs on the main/update path.
  - Makes the newest `FrameData` visible to VSG render objects and ImGui.
  - Schedules scene additions/removals based on diff data from the simulator.

- `AddSceneObjectOperation`
  - Runs on main/update path.
  - Creates or attaches a VSG `SceneObject` node for a simulator object id.
  - Compiles resources if required and calls `updateViewer(*viewer, compileResult)` before attachment.

- `RemoveSceneObjectOperation`
  - Runs on main/update path.
  - Removes an object id from the attachment group and object registry.

Open implementation choice:

- Use fixed-rate simulation independent of render FPS.
- Prefer one continuously rescheduled `SimulationStepOperation` over long-running custom thread logic, unless VSG operation queues make per-frame scheduling too noisy.

## AppData / Event Hub

Use an `AppData` class for communication between event handlers, simulator operations, render objects, and ImGui.

`AppData` is useful for this app because it gives the MVP one explicit place to document and enforce thread boundaries:

- Main thread publishes app events from VSG event handlers and ImGui controls.
- Simulator operation thread consumes simulator-directed events.
- Simulator operation thread publishes immutable `FrameData` snapshots.
- Main/update path consumes the latest `FrameData` and applies render object updates.
- ImGui reads the same published `FrameData` snapshot as the render object update path.

MVP API sketch:

```cpp
class AppData
{
public:
    void publishEvent(AppEvent event);
    std::vector<AppEvent> takeSimulatorEvents();

    void publishFrame(std::shared_ptr<const FrameData> frame);
    std::shared_ptr<const FrameData> latestFrame() const;

    void publishUiCommand(UiCommand command);
    std::vector<UiCommand> takeUiCommands();
};
```

Important ownership rule:

- `AppData` owns queues and snapshot handoff only.
- The simulator owns mutable simulation state.
- The VSG app owns viewer, scene graph, object registry, and ImGui objects.
- Do not put mutable scene object containers in `AppData` if both simulator and renderer can access them.

## Synchronization Plan

VSG helps with operation scheduling, but it does not make app-owned data automatically thread-safe.

VSG-provided synchronization we can rely on:

- `vsg::OperationQueue` is a thread-safe queue using an internal mutex and condition variable.
- `vsg::OperationThreads` shares that thread-safe queue across worker threads.
- `vsg::UpdateOperations` is thread-safe for adding/removing operations and runs them from `Viewer::update()`.

App-owned synchronization still required:

- `AppData` event queues need a `std::mutex`, unless every event is transported only as a `vsg::OperationQueue` operation.
- Latest `FrameData` handoff needs synchronization. Use either a mutex-protected `std::shared_ptr<const FrameData>` or C++20 atomic shared pointer operations.
- Simple counters/flags such as `paused`, `quitRequested`, `simulationFrame`, or backlog counts can use `std::atomic`.
- Compound state such as vectors of events, vectors of object states, and UI command batches should use a mutex and swap-copy pattern.

Preferred MVP approach:

- Use `std::mutex` for event queues and latest-frame pointer because it is clear and hard to misuse.
- Keep locks short by swapping vectors/shared pointers, never by running physics, rendering ImGui, compiling VSG objects, or mutating the scene while holding an `AppData` lock.
- Use atomics only for tiny scalar flags/counters that do not need to stay consistent with other fields.

## Data Model

Use stable ids for all simulated/rendered objects.

MVP frame payload decision:

- Use minimum practical data: `FrameData` carries updated object state for active objects that changed plus explicit created/removed ids.
- Keep enough aggregate counts/stats in every frame for ImGui.
- The main-thread object registry keeps render-side object ownership and applies diffs.

```cpp
enum class ObjectType
{
    Cube,
    Sphere
};

struct ObjectState
{
    uint64_t id;
    ObjectType type;
    vsg::dvec3 position;
    vsg::dvec3 velocity;
    vsg::dquat orientation;
    vsg::dvec3 scale;
    vsg::vec4 color;
};

struct FrameData
{
    uint64_t simulationFrame;
    double simulationTimeSeconds;
    double simulationDeltaSeconds;
    double fps;
    uint64_t totalObjects;
    uint64_t cubeCount;
    uint64_t sphereCount;
    uint64_t collisionCount;
    uint64_t createdThisFrame;
    uint64_t removedThisFrame;
    std::vector<ObjectState> objects;
    std::vector<uint64_t> removedObjectIds;
};
```

Frame data rule:

- `FrameData` is immutable after publish.
- Render-side classes read from `std::shared_ptr<const FrameData>` or `vsg::ref_ptr<const FrameDataObject>`.
- No render object stores references into simulator-owned containers.

## Simulator Scope

MVP simulator behavior:

- Fixed or semi-fixed timestep.
- Default object cap is 100.
- Randomly spawn spheres and cubes up to a configurable object limit.
- Spheres have velocity and bounce inside a bounded floor area.
- Cubes are initially static but have velocity/impulse state after collisions.
- Sphere/cube collision is simple bounding sphere or AABB approximation.
- Sphere impact transfers a small impulse to cube velocity.
- Cubes slow down with friction.
- Objects expire automatically by age and/or bounds to demonstrate removal.

Keep physics intentionally simple. The app is demonstrating threading and scene handoff, not a physics engine.

## Scene Object API

Render objects should have a small, explicit lifecycle:

```cpp
class SceneObject
{
public:
    virtual ~SceneObject() = default;
    virtual void init(vsg::ref_ptr<vsg::Group> parent, vsg::ref_ptr<vsg::Viewer> viewer) = 0;
    virtual void update(const FrameData& frameData, const ObjectState& objectState) = 0;
    virtual uint64_t id() const = 0;
};
```

Likely concrete classes:

- `CubeSceneObject`
- `SphereSceneObject`
- Optional `FloorSceneObject`

MVP VSG representation:

- Each object owns a `vsg::MatrixTransform`.
- Use the simplest available geometry path: VSG builder helpers if they fit the installed version, otherwise small procedural cube/sphere geometry.
- Per-frame movement updates only transform matrices on the main/update path.
- Geometry and pipeline state are shared where possible.
- Prefer shared geometry/material resources and an object pool over compiling unique geometry per object.

## ImGui API

ImGui should be represented as an app UI object plus a VSG command:

```cpp
class StatsUi
{
public:
    void init();
    void render(const FrameData& frameData);
};

class StatsGuiCommand : public vsg::Inherit<vsg::Command, StatsGuiCommand>
{
public:
    void record(vsg::CommandBuffer& commandBuffer) const override;
};
```

`StatsGuiCommand::record(...)` calls `StatsUi::render(frameData)` and is passed to `vsgImGui::RenderImGui::create(window, command)`, following the existing `MainWindowGui` pattern in `vkvsg`/`vsgdock`.

Stats panel MVP fields:

- FPS.
- VSG frame count.
- Simulation frame count.
- Total scene objects.
- Cube count.
- Sphere count.
- Collision count.
- Objects created this frame.
- Objects removed this frame.
- Operation queue/backlog counters if practical.
- Pause/resume simulation toggle.
- Spawn rate or max object slider if practical.
- Controls publish `AppEvent`/`UiCommand` messages through `AppData`; they do not directly mutate simulator state.

## Input/Event Flow

MVP interaction:

- ImGui controls publish app events such as pause, spawn-rate changes, spawn bursts, and clear-scene requests.
- Direct floor-click object creation is removed so object creation can be driven by simulator settings and UI form controls.

Event flow:

- Main-thread UI code pushes app events into a thread-safe app event queue.
- Simulator consumes app events during the next simulation step.
- Simulator includes created, updated, and removed objects in the next `FrameData`.
- Main/update path sees the frame diff and enqueues/adds/removes VSG `SceneObject` nodes.

## File/Target Plan

Expected files:

- `src/vsgthreading/main.cpp`
- `src/vsgthreading/VsgThreadingApp.h`
- `src/vsgthreading/VsgThreadingApp.cpp`
- `src/vsgthreading/FrameData.h`
- `src/vsgthreading/Simulator.h`
- `src/vsgthreading/Simulator.cpp`
- `src/vsgthreading/SceneObject.h`
- `src/vsgthreading/SceneObject.cpp`
- `src/vsgthreading/StatsUi.h`
- `src/vsgthreading/StatsUi.cpp`
- `src/vsgthreading/todo.md`

CMake:

- Add `vsgthreading` under `if(VKRAW_BUILD_VKVSG)`.
- Link `vsg::vsg` and `vsgImGui::vsgImGui`.
- Link `vsgXchange::vsgXchange` only if later needed.
- Reuse `VSG_DEPS_INSTALL_DIR` and platform compile definitions used by `vkvsg`.

## Implementation Checklist

- [x] Define MVP architecture in this TODO.
- [x] Answer MVP blocking questions below.
- [x] Add `vsgthreading` CMake target.
- [x] Create app skeleton with VSG window, camera, scene root, floor, and viewer loop.
- [x] Add ImGui integration with `StatsGuiCommand`.
- [x] Add `FrameData` and thread-safe latest-frame handoff.
- [x] Add simulator state and `SimulationStepOperation`.
- [x] Add main-thread publish/update operation.
- [x] Add `SceneObject` registry keyed by simulator id.
- [x] Implement cube and sphere scene objects.
- [x] Add random object spawning.
- [x] Add simple sphere/cube collision and cube impulse response.
- [x] Add object removal path.
- [x] Add app event queue.
- [x] Remove floor-click cube creation.
- [x] Show stats in ImGui.
- [x] Build with CMake.
- [ ] Run a smoke test if a display is available. Attempted `./build/vsgCef -f 3`; this environment failed at XCB window creation.
- [x] Document the final threading flow in a short README section or comments.

## Questions Before Implementation

- Should `vsgthreading` use installed `vsgImGui::vsgImGui` like `vkvsg`, or the local docking `vsgImGuiDocking` library like `vsgdock`? use the vsgImGui::vsgImGui.
- Should the simulator run at a fixed rate independent of render FPS, or one simulation step per rendered frame for easier demonstration? fixed rate independent of render.
- Should `FrameData` contain the full object list every frame, or should it contain a full list plus explicit add/remove/update diffs? best option for minimum data.
- Is an approximate collision model acceptable for the MVP, or do we need visibly accurate cube/sphere collision boundaries? approx collision.
- What object cap should the MVP target by default: 100, 500, 1000, or something else? 100 objects, maybe objects expire on sim after time.
- Should sphere/cube geometry be generated procedurally in code, or should we use VSG builder helpers if available in the installed version? use simple option.
- Should floor picking use a manual ray/plane intersection, or should it use VSG intersectors to demonstrate scene picking APIs? manual ray/plane or simplest.
- Should object creation compile one shared cube/sphere geometry up front, or should add-object operations demonstrate per-object compile/merge even if less efficient? best option, instances, object pool.
- Should ImGui controls be read-only stats for MVP, or include live controls such as pause, spawn rate, and clear scene? I would like imgui controls to create appevents that are handled, so live controls.
- Should removal be automatic by lifetime/bounds, user-driven through the UI, or both? automatic.
