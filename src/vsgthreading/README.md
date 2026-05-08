# vsgthreading

`vsgthreading` is a small VSG sample app for demonstrating a VSG-idiomatic threaded application structure.

## Thread Ownership

- The main thread owns `vsg::Viewer`, windows, event handlers, command graphs, ImGui recording, and the attached scene graph.
- Worker threads are created with `vsg::OperationThreads`.
- Worker operations advance the simulator and publish immutable `FrameData` snapshots through `AppData`.
- Worker operations do not mutate the attached VSG scene graph.
- Main-thread update operations run from `viewer->update()` and apply `FrameData` diffs to VSG `SceneObject` nodes.

## Data Flow

1. VSG event handlers and ImGui controls publish `AppEvent` values to `AppData`.
2. `SimulationStepOperation` runs on a VSG operation thread at a fixed simulation rate.
3. The simulator consumes pending app events, advances simple cube/sphere physics, and publishes a `FrameData` diff.
4. The worker schedules `PublishFrameOperation` with `viewer->addUpdateOperation(...)`.
5. `PublishFrameOperation` runs during `viewer->update()` on the main thread.
6. The render-side object registry creates, updates, and removes `SceneObject` nodes from the diff.
7. `StatsGuiCommand::record(...)` renders ImGui from the latest published frame.

## Synchronization

VSG provides thread-safe operation queues, but app-owned data still needs synchronization. `AppData` uses short mutex-protected handoff methods for event queues and frame snapshots. The simulator owns mutable simulation state, and the main VSG app owns mutable render state.

## Shaders

The primitive geometry path intentionally does not use `vsg::Builder`, because this local VSG install was built without GLSLang runtime shader compilation. The app uses the repo's precompiled `equator_line.vert.spv` and `equator_line.frag.spv` shaders through a small explicit VSG graphics pipeline.
