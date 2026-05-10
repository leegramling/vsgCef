# Vulkan Scene Graph Multi-Threading

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

Functions consuming this data often use const references:

```cpp
void processState(const ObjectState& state)
{
    ...
}
```

The const reference communicates an important rule: the receiver may read the object but may not modify it. This helps establish clear ownership boundaries between systems.

Eventually however, the renderer must apply mutable changes to scene graph objects such as transforms or geometry. VSG applications commonly solve this by allowing worker threads to push immutable updates into a `vsg::ThreadSafeQueue<T>` while the render thread drains and applies those updates during the frame update traversal.

For example, a simulation thread may generate updates:

```cpp
vsg::ThreadSafeQueue<ObjectState> queue;

queue.add(state);
```

Then during `viewer->update()`:

```cpp
auto updates = queue.take_all();

for (auto& state : updates)
{
    auto itr = transforms.find(state.id);

    if (itr != transforms.end())
    {
        itr->second->matrix =
            vsg::translate(state.position);
    }
}
```

This architecture keeps mutable scene graph operations isolated to the render thread while worker threads remain free to continuously generate data. The queue becomes a synchronization boundary between asynchronous systems.

One of the strengths of this approach is that it scales naturally. Additional worker systems such as networking, AI, terrain streaming, or browser rendering can all produce frame updates independently without directly touching Vulkan resources or scene graph traversal state.

The result is a cleaner separation between simulation and rendering while still allowing VSG to maintain a stable and predictable rendering pipeline.
