#include "vsgthreading/VsgThreadingApp.h"

#include "vsgthreading/FrameData.h"
#include "vsgthreading/Profiling.h"
#include "vsgthreading/SceneObject.h"
#include "vsgthreading/Simulator.h"
#include "vsgthreading/StatsUi.h"

#ifdef VSGCEF_ENABLE_CEF_RUNTIME
#include "vsgcef/CefUi.h"
#endif

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace vsgcef {
class CefUi;
}

namespace vsgthreading {
namespace {
using Clock = std::chrono::steady_clock;

struct RenderState : public vsg::Inherit<vsg::Object, RenderState>
{
    vsg::ref_ptr<vsg::Group> dynamicGroup;
    vsg::ref_ptr<vsg::Node> cubePrototype;
    vsg::ref_ptr<vsg::Node> spherePrototype;
    std::unordered_map<uint64_t, vsg::ref_ptr<SceneObject>> objects;
    std::shared_ptr<const FrameData> currentFrame;
    double renderFps = 0.0;
};

void removeChild(vsg::Group& group, const vsg::ref_ptr<vsg::Node>& node)
{
    auto& children = group.children;
    children.erase(std::remove(children.begin(), children.end(), node), children.end());
}

vsg::ref_ptr<vsg::StateGroup> createPipelineStateGroup()
{
    const std::string vertPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.vert.spv";
    const std::string fragPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.frag.spv";
    auto vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vertPath);
    auto fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragPath);
    if (!vertexShader || !fragmentShader)
    {
        std::cerr << "[vsgCef] Failed to load shaders from " << VKVSG_SHADER_DIR << std::endl;
        return {};
    }

    vsg::VertexInputState::Bindings bindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX}};

    vsg::VertexInputState::Attributes attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;
    rasterizationState->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE),
        rasterizationState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, pushConstantRanges);
    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(vsg::BindGraphicsPipeline::create(graphicsPipeline));
    return stateGroup;
}

vsg::ref_ptr<vsg::Node> createIndexedGeometry(const std::vector<vsg::vec3>& vertexValues,
                                              const std::vector<uint16_t>& indexValues,
                                              const vsg::vec4& color)
{
    auto stateGroup = createPipelineStateGroup();
    if (!stateGroup) return {};

    auto vertices = vsg::vec3Array::create(static_cast<uint32_t>(vertexValues.size()));
    for (std::size_t i = 0; i < vertexValues.size(); ++i) (*vertices)[i] = vertexValues[i];
    auto colors = vsg::vec4Array::create(vertexValues.size());
    for (auto& c : *colors) c = color;
    auto indices = vsg::ushortArray::create(static_cast<uint32_t>(indexValues.size()));
    for (std::size_t i = 0; i < indexValues.size(); ++i) (*indices)[i] = indexValues[i];

    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{vertices, colors});
    draw->assignIndices(indices);
    draw->indexCount = static_cast<uint32_t>(indices->size());
    draw->instanceCount = 1;

    stateGroup->addChild(draw);
    return stateGroup;
}

vsg::ref_ptr<vsg::Node> createCubeNode(const vsg::vec4& color)
{
    std::vector<vsg::vec3> vertices{
        {-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}};

    std::vector<uint16_t> indices{
        4, 5, 6, 6, 7, 4,
        1, 0, 3, 3, 2, 1,
        0, 4, 7, 7, 3, 0,
        5, 1, 2, 2, 6, 5,
        3, 7, 6, 6, 2, 3,
        0, 1, 5, 5, 4, 0};
    return createIndexedGeometry(vertices, indices, color);
}

vsg::ref_ptr<vsg::Node> createFloorNode()
{
    std::vector<vsg::vec3> vertices{
        {-10.0f, -10.0f, 0.0f},
        {10.0f, -10.0f, 0.0f},
        {10.0f, 10.0f, 0.0f},
        {-10.0f, 10.0f, 0.0f}};
    std::vector<uint16_t> indices{0, 1, 2, 2, 3, 0};
    return createIndexedGeometry(vertices, indices, vsg::vec4(0.28f, 0.32f, 0.30f, 1.0f));
}

vsg::ref_ptr<vsg::Node> createSphereNode(const vsg::vec4& color)
{
    constexpr uint32_t rings = 8;
    constexpr uint32_t sectors = 16;
    std::vector<vsg::vec3> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve((rings + 1) * sectors);

    for (uint32_t r = 0; r <= rings; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(rings);
        const double phi = (v - 0.5) * vsg::PI;
        const double z = std::sin(phi) * 0.5;
        const double ringRadius = std::cos(phi) * 0.5;
        for (uint32_t s = 0; s < sectors; ++s)
        {
            const double u = static_cast<double>(s) / static_cast<double>(sectors);
            const double theta = u * 2.0 * vsg::PI;
            vertices.emplace_back(static_cast<float>(std::cos(theta) * ringRadius),
                                  static_cast<float>(std::sin(theta) * ringRadius),
                                  static_cast<float>(z));
        }
    }

    for (uint32_t r = 0; r < rings; ++r)
    {
        for (uint32_t s = 0; s < sectors; ++s)
        {
            const uint16_t a = static_cast<uint16_t>(r * sectors + s);
            const uint16_t b = static_cast<uint16_t>(r * sectors + ((s + 1) % sectors));
            const uint16_t c = static_cast<uint16_t>((r + 1) * sectors + ((s + 1) % sectors));
            const uint16_t d = static_cast<uint16_t>((r + 1) * sectors + s);
            indices.insert(indices.end(), {a, b, c, c, d, a});
        }
    }

    return createIndexedGeometry(vertices, indices, color);
}

class PublishFrameOperation : public vsg::Inherit<vsg::Operation, PublishFrameOperation>
{
public:
    PublishFrameOperation(std::shared_ptr<AppData> appData, vsg::observer_ptr<vsg::Viewer> viewer, vsg::ref_ptr<RenderState> renderState) :
        appData_(std::move(appData)),
        viewer_(viewer),
        renderState_(std::move(renderState))
    {
    }

    void run() override
    {
        VSGCEF_ZONE("PublishFrameOperation::run");
        VSGCEF_THREAD_NAME("vsg update");

        auto frame = appData_ ? appData_->takePendingFrame() : nullptr;
        if (!frame || !renderState_ || !renderState_->dynamicGroup) return;

        auto viewer = vsg::ref_ptr<vsg::Viewer>(viewer_);
        renderState_->currentFrame = frame;

        for (uint64_t id : frame->removedObjectIds)
        {
            VSGCEF_ZONE("Detach removed scene objects");
            auto it = renderState_->objects.find(id);
            if (it == renderState_->objects.end()) continue;
            removeChild(*renderState_->dynamicGroup, it->second->node());
            renderState_->objects.erase(it);
        }

        for (const auto& state : frame->createdObjects)
        {
            VSGCEF_ZONE("Create and compile scene object");
            auto prototype = state.type == ObjectType::Cube ? renderState_->cubePrototype : renderState_->spherePrototype;
            auto object = SceneObject::create(state.id, state.type, prototype);
            object->update(state);
            if (viewer && viewer->compileManager)
            {
                auto result = viewer->compileManager->compile(object->node());
                if (result) updateViewer(*viewer, result);
            }
            object->init(renderState_->dynamicGroup);
            renderState_->objects[state.id] = object;
        }

        for (const auto& state : frame->updatedObjects)
        {
            VSGCEF_ZONE("Update scene object transform");
            auto it = renderState_->objects.find(state.id);
            if (it != renderState_->objects.end()) it->second->update(state);
        }
    }

private:
    std::shared_ptr<AppData> appData_;
    vsg::observer_ptr<vsg::Viewer> viewer_;
    vsg::ref_ptr<RenderState> renderState_;
};

class SimulationStepOperation : public vsg::Inherit<vsg::Operation, SimulationStepOperation>
{
public:
    SimulationStepOperation(std::shared_ptr<AppData> appData,
                            std::shared_ptr<Simulator> simulator,
                            vsg::observer_ptr<vsg::Viewer> viewer,
                            vsg::ref_ptr<vsg::OperationThreads> workers,
                            vsg::ref_ptr<RenderState> renderState,
                            Clock::time_point nextTick) :
        appData_(std::move(appData)),
        simulator_(std::move(simulator)),
        viewer_(viewer),
        workers_(std::move(workers)),
        renderState_(std::move(renderState)),
        nextTick_(nextTick)
    {
    }

    void run() override
    {
        VSGCEF_ZONE("SimulationStepOperation::run");
        VSGCEF_THREAD_NAME("simulation operation");

        constexpr double dt = 1.0 / 60.0;
        constexpr auto tick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(dt));
        {
            VSGCEF_ZONE("Simulation tick sleep");
            std::this_thread::sleep_until(nextTick_);
        }

        auto viewer = vsg::ref_ptr<vsg::Viewer>(viewer_);
        if (!viewer || !viewer->status || viewer->status->cancel()) return;

        const std::size_t pendingEvents = appData_->pendingEventCount();
        const std::size_t backlog = workers_ && workers_->queue && !workers_->queue->empty() ? 1u : 0u;
        std::vector<AppEvent> events;
        {
            VSGCEF_ZONE("Take simulator events");
            events = appData_->takeSimulatorEvents();
        }

        std::shared_ptr<const FrameData> frame;
        {
            VSGCEF_ZONE("Simulator::step");
            frame = simulator_->step(dt, std::move(events), pendingEvents, backlog);
        }

        {
            VSGCEF_ZONE("Publish simulator frame");
            appData_->publishFrame(frame);
        }

        viewer->addUpdateOperation(PublishFrameOperation::create(appData_, viewer_, renderState_));

        if (workers_ && workers_->status && workers_->status->active())
        {
            workers_->add(SimulationStepOperation::create(appData_, simulator_, viewer_, workers_, renderState_, nextTick_ + tick));
        }
    }

private:
    std::shared_ptr<AppData> appData_;
    std::shared_ptr<Simulator> simulator_;
    vsg::observer_ptr<vsg::Viewer> viewer_;
    vsg::ref_ptr<vsg::OperationThreads> workers_;
    vsg::ref_ptr<RenderState> renderState_;
    Clock::time_point nextTick_;
};

class StatsGuiCommand : public vsg::Inherit<vsg::Command, StatsGuiCommand>
{
public:
    StatsGuiCommand(std::shared_ptr<AppData> appData,
                    vsg::ref_ptr<RenderState> renderState,
                    std::shared_ptr<vsgcef::CefUi> cefUi = {},
                    vsg::observer_ptr<vsg::Viewer> viewer = {}) :
        appData_(std::move(appData)),
        renderState_(std::move(renderState)),
        cefUi_(std::move(cefUi)),
        viewer_(viewer),
        ui_(std::make_shared<StatsUi>(appData_, cefUi_, viewer_))
    {
        ui_->init();
    }

    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        VSGCEF_ZONE("StatsGuiCommand::record");

        FrameData fallback;
        const FrameData* frame = &fallback;
        if (renderState_ && renderState_->currentFrame) frame = renderState_->currentFrame.get();

        FrameData displayFrame = *frame;
        if (renderState_) displayFrame.renderFps = renderState_->renderFps;
        ui_->render(displayFrame, commandBuffer.deviceID);
    }

private:
    std::shared_ptr<AppData> appData_;
    vsg::ref_ptr<RenderState> renderState_;
    std::shared_ptr<vsgcef::CefUi> cefUi_;
    vsg::observer_ptr<vsg::Viewer> viewer_;
    std::shared_ptr<StatsUi> ui_;
};

vsg::ref_ptr<vsg::Node> createScene(vsg::ref_ptr<RenderState> renderState)
{
    auto root = vsg::Group::create();
    renderState->cubePrototype = createCubeNode(vsg::vec4(0.25f, 0.68f, 0.94f, 1.0f));
    renderState->spherePrototype = createSphereNode(vsg::vec4(0.96f, 0.48f, 0.24f, 1.0f));
    root->addChild(createFloorNode());
    renderState->dynamicGroup = vsg::Group::create();
    root->addChild(renderState->dynamicGroup);
    return root;
}

} // namespace

int VsgThreadingApp::run(int argc, char** argv)
{
    try
    {
        VSGCEF_THREAD_NAME("main");

        vsg::CommandLine arguments(&argc, argv);
        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vsgCef";
        windowTraits->width = 1280;
        windowTraits->height = 800;
        windowTraits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

        const int numFrames = arguments.value(-1, "-f");
        const uint32_t numWorkerThreads = arguments.value(1u, "-n");
        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto appData = std::make_shared<AppData>();

#ifdef VSGCEF_ENABLE_CEF_RUNTIME
        auto cefCommandHandler = [appData](const vsgcef::CefUiCommand& command, std::string& errorMessage) {
            if (command.type == "setPaused")
            {
                appData->publishEvent(SetPausedEvent{command.paused});
                return true;
            }
            if (command.type == "setSpawnRate")
            {
                appData->publishEvent(SetSpawnRateEvent{command.objectsPerSecond});
                return true;
            }
            if (command.type == "spawnBurst")
            {
                appData->publishEvent(SpawnBurstEvent{command.count});
                return true;
            }
            if (command.type == "clearObjects")
            {
                appData->publishEvent(ClearObjectsEvent{});
                return true;
            }
            if (command.type == "mockSettingChanged" ||
                command.type == "mockTypeEnabledChanged" ||
                command.type == "mockTypeSpawnChanged" ||
                command.type == "mockTypeSpeedChanged")
            {
                return true;
            }

            errorMessage = "Unhandled CEF command: " + command.type;
            return false;
        };

        auto cefUi = vsgcef::CefUi::create(argc, argv, VSGCEF_CEF_UI_DIR, cefCommandHandler);
        if (cefUi && cefUi->exitCode() >= 0) return cefUi->exitCode();
        if (cefUi && cefUi->initialized())
            cefUi->createBrowsers();
        else
            cefUi.reset();
#else
        std::shared_ptr<vsgcef::CefUi> cefUi;
#endif

        auto simulator = std::make_shared<Simulator>();
        auto renderState = RenderState::create();
        auto scene = createScene(renderState);

        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cerr << "Could not create VSG window." << std::endl;
            return 1;
        }

        auto viewer = vsg::Viewer::create();
        viewer->addWindow(window);

        constexpr uint32_t leftStatsPanelWidth = 300;
        const auto windowExtent = window->extent2D();
        const uint32_t sceneViewportX = std::min(leftStatsPanelWidth, windowExtent.width);
        const uint32_t sceneViewportWidth = std::max(1u, windowExtent.width - sceneViewportX);

        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, -18.0, 12.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
        auto perspective = vsg::Perspective::create(45.0,
                                                    static_cast<double>(sceneViewportWidth) / static_cast<double>(windowExtent.height),
                                                    0.1,
                                                    100.0);
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(static_cast<int32_t>(sceneViewportX), 0, sceneViewportWidth, windowExtent.height));

        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));
        viewer->addEventHandler(vsg::Trackball::create(camera));

        auto view = vsg::View::create(camera);
        view->addChild(vsg::createHeadlight());
        view->addChild(scene);

        auto renderGraph = vsg::RenderGraph::create(window, view);
        renderGraph->clearValues[0].color = vsg::sRGB_to_linear(0.10f, 0.11f, 0.12f, 1.0f);
        IMGUI_CHECKVERSION();
        if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
        ImGui::GetIO().MouseDrawCursor = true;
        renderGraph->addChild(vsgImGui::RenderImGui::create(window, StatsGuiCommand::create(appData, renderState, cefUi, vsg::observer_ptr<vsg::Viewer>(viewer))));

        auto commandGraph = vsg::CommandGraph::create(window);
        commandGraph->addChild(renderGraph);
        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

        auto resourceHints = vsg::ResourceHints::create();
        resourceHints->numDescriptorSets = 256;
        resourceHints->descriptorPoolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256});
        viewer->compile(resourceHints);

        auto workers = vsg::OperationThreads::create(std::max(1u, numWorkerThreads), viewer->status);
        appData->publishEvent(SpawnBurstEvent{16});
        workers->add(SimulationStepOperation::create(appData, simulator, vsg::observer_ptr<vsg::Viewer>(viewer), workers, renderState, Clock::now()));

        auto lastFrameTime = Clock::now();
        int framesRemaining = numFrames;
        while (viewer->advanceToNextFrame() && (numFrames < 0 || framesRemaining-- > 0))
        {
            VSGCEF_ZONE("vsgCef frame");
            VSGCEF_FRAME_MARK("vsgCef frame");

            const auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - lastFrameTime).count();
            lastFrameTime = now;
            renderState->renderFps = dt > 0.0 ? 1.0 / dt : 0.0;

            if (cefUi)
            {
                VSGCEF_ZONE("CEF message loop work");
                cefUi->doMessageLoopWork();
            }
            {
                VSGCEF_ZONE("viewer->handleEvents");
                viewer->handleEvents();
            }
            {
                VSGCEF_ZONE("viewer->update");
                viewer->update();
            }
            {
                VSGCEF_ZONE("viewer->recordAndSubmit");
                viewer->recordAndSubmit();
            }
            {
                VSGCEF_ZONE("viewer->present");
                viewer->present();
            }
        }

        workers->stop();
    }
    catch (const vsg::Exception& exception)
    {
        std::cerr << "[vsgCef] VSG exception: " << exception.message << " result = " << exception.result << std::endl;
        return 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[vsgCef] exception: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}

} // namespace vsgthreading
