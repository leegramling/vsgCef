# VSG Multi-Threading and Application Structure

Vulkan Scene Graph applications are often structured differently than traditional game engines. In many older rendering engines, the renderer, simulation, input handling, and scene updates all occur inside one large while loop. As applications become more advanced however, especially simulation or visualization systems, this structure becomes harder to maintain. A simulator may need to continuously generate data while the renderer must maintain a stable frame rate and safely interact with Vulkan resources.

VSG generally approaches this problem by keeping the viewer and rendering system on the main thread while allowing background work to occur on worker threads. This is important because the rendering loop owns the scene graph traversal, Vulkan command submission, and platform event processing. Keeping these systems centralized avoids a large amount of synchronization complexity and prevents multiple threads from modifying scene graph state at the same time.

A typical VSG frame loop looks similar to:

```cpp
while (viewer->advanceToNextFrame())
{
    viewer->handleEvents();

    viewer->update();

    viewer->recordAndSubmit();

    viewer->present();
}
```

The `handleEvents()` phase processes mouse, keyboard, and window events. Internally, VSG uses event callbacks and event handlers derived from `vsg::Visitor`. This allows applications to attach custom event systems without tightly coupling them to the rendering code.

For example:

```cpp
class MyEventHandler : public vsg::Inherit<vsg::Visitor, MyEventHandler>
{
public:

    void apply(vsg::KeyPressEvent& keyPress) override
    {
        if (keyPress.keyBase == 'r')
        {
            resetSimulation();
        }
    }
};
```

The important design idea here is that event handling remains lightweight and deterministic. The callbacks usually should not perform heavy simulation or long-running operations directly inside the event traversal. Instead, they often signal worker systems or enqueue requests for later processing.

This is where `vsg::Operation` becomes useful. VSG provides `vsg::Operation` as a base class for asynchronous work. Rather than placing Vulkan rendering onto another thread, applications commonly place simulation systems, terrain loading, networking, browser processing, or background computation onto worker threads and allow the renderer to consume the results.

A simplified operation might look like this:

```cpp
class SimulationOperation : public vsg::Operation
{
public:

    void run() override
    {
        while (running)
        {
            SimulationFrame frame;

            updateSimulation(frame);

            queue->add(frame);
        }
    }
};
```

VSG also provides `vsg::ThreadSafeQueue<T>`, which is a synchronized queue implementation for passing data safely between threads. Internally, `vsg::OperationQueue` is simply a typedef of:

```cpp
vsg::ThreadSafeQueue<vsg::ref_ptr<vsg::Operation>>
```

The queue supports adding objects from one thread while another thread consumes them. One particularly useful function is `take_all()`, which allows the consumer side to drain all pending items at once during a known synchronization point in the frame.

This creates a clean producer-consumer architecture where the simulation and rendering systems remain loosely coupled.

One of the most important concepts in multithreaded rendering systems is ownership of mutable data. A common mistake is allowing both the simulation thread and render thread to directly modify the same scene graph objects simultaneously. Even with mutexes, this quickly becomes difficult to reason about and can introduce race conditions or frame stalls.

Instead, it is usually better to pass immutable snapshot-style data between systems. Small structures such as transforms, sensor readings, or object states can safely be copied between threads.

For example:

```cpp
struct ObjectState
{
    uint32_t id;

    glm::dvec3 position;
};
```

It is important to note that the `ObjectState` itself is not being simultaneously modified by multiple threads. In this design, the simulation thread creates a new `ObjectState` snapshot, populates its values, and then passes it through the queue. After the snapshot is queued, that instance is treated as immutable. This avoids data races because the render thread consumes a stable copy of the data rather than reading from an object that another thread is still modifying.

Functions consuming this data often use const references:

```cpp
void processState(const ObjectState& state)
{
    ...
}
```

The const reference communicates an important rule: the receiver may read the object but may not modify it. However, `const` alone does not make code thread safe. If another thread were still modifying the same underlying object simultaneously, synchronization primitives such as mutexes or atomics would still be required. In this design, safety comes from immutable snapshots and queue ownership rather than from the `const` keyword itself.

## Using a Queue Between Threads

A queue is one of the cleanest ways to pass data between a simulator thread and the VSG render thread. The important idea is that the simulator does not directly modify the scene graph. Instead, it creates small data objects that describe what changed, places those objects into a thread-safe queue, and then lets the render thread apply those changes during a safe point in the frame.

For example, the simulator might create an `ObjectState` containing the object id and its new world position. The simulator fills out the structure completely before adding it to the queue. After the object has been added to the queue, the simulator treats that specific update as finished. It does not keep modifying the same instance while the render thread is reading it.

```cpp
struct ObjectState
{
    uint32_t id;
    glm::dvec3 worldPosition;
};

vsg::ThreadSafeQueue<ObjectState> renderUpdates;
```

The simulator thread produces updates:

```cpp
ObjectState state;
state.id = aircraftId;
state.worldPosition = computedWorldPosition;

renderUpdates.add(state);
```

Later, the render thread drains the queue during the update phase:

```cpp
auto updates = renderUpdates.take_all();

for (const ObjectState& state : updates)
{
    auto itr = transforms.find(state.id);

    if (itr != transforms.end())
    {
        itr->second->matrix = vsg::translate(state.worldPosition);
    }
}
```

This is safer than sharing one mutable `ObjectState&` between threads. A `const ObjectState&` only prevents modification through that particular reference. It does not stop another thread from modifying the original object at the same time. The safe pattern is that the simulator creates a snapshot, queues it, and then the render thread consumes that stable snapshot.

In this design, the queue is the ownership boundary. The producer thread owns the update while it is building it. Once it calls `add()`, the queue owns a copy or moved value. Later, the render thread takes ownership when it drains the queue with `take_all()`. This creates a simple rule: worker threads produce data, but the render thread mutates the VSG scene graph.

## Sending Events Back to the Simulator

Data also often needs to flow in the opposite direction. The render thread handles user input, picking, UI buttons, and scene interaction. If the user clicks on the scene and wants to add an object at a world position, that request should usually be sent to the simulator rather than immediately changing simulation state directly from the UI code.

For this, you can use another queue going from the render thread to the simulator thread. The render thread creates a command or event object describing what the user requested. The simulator drains that queue during its own update step and decides how to modify the simulation model.

```cpp
struct SimulationEvent
{
    enum class Type
    {
        AddObject,
        RemoveObject
    };

    Type type;
    uint32_t id = 0;
    glm::dvec3 worldPosition {};
};

vsg::ThreadSafeQueue<SimulationEvent> simulationEvents;
```

When the user clicks in the scene, the render thread can convert the mouse position into a world-space position using the application’s picking logic. It then queues an event for the simulator:

```cpp
void onSceneClick(const glm::dvec3& worldPosition)
{
    SimulationEvent event;
    event.type = SimulationEvent::Type::AddObject;
    event.worldPosition = worldPosition;

    simulationEvents.add(event);
}
```

The simulator thread later drains these events:

```cpp
auto events = simulationEvents.take_all();

for (const SimulationEvent& event : events)
{
    if (event.type == SimulationEvent::Type::AddObject)
    {
        simulator.addObject(event.worldPosition);
    }
    else if (event.type == SimulationEvent::Type::RemoveObject)
    {
        simulator.removeObject(event.id);
    }
}
```

This creates a two-way message flow. The simulator sends render updates to the render thread, and the render thread sends user commands back to the simulator. Each thread owns its own mutable state, and the queues pass small immutable messages between them.

The render thread owns the VSG scene graph. The simulator thread owns the simulation model. The queues are the synchronization points between the two systems. This keeps the architecture predictable and avoids the dangerous pattern where the UI, renderer, and simulator all directly mutate the same objects.

One of the strengths of this approach is that it scales naturally. Additional worker systems such as networking, AI, terrain streaming, or browser rendering can all produce frame updates independently without directly touching Vulkan resources or scene graph traversal state.

The result is a cleaner separation between simulation and rendering while still allowing VSG to maintain a stable and predictable rendering pipeline.
