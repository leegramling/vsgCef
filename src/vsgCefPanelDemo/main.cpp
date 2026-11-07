#include "htmlui/Json.h"
#include "vsgcef/UIPanel.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <iomanip>

namespace {
struct SceneCommand
{
    enum class Type { AddCubes, SetCubePosition, Exit } type = Type::AddCubes;
    uint32_t count = 0;
    uint32_t cubeId = 0;
    vsg::vec3 position;
};
struct CubeEntry
{
    uint32_t id = 0;
    vsg::ref_ptr<vsg::MatrixTransform> transform;
    vsg::ref_ptr<vsg::vec4Array> colors;
    vsg::vec4 baseColor;
    vsg::vec3 position;
};
struct State
{
    std::shared_ptr<vsgcef::CefUi> cefUi;
    vsg::ref_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Group> scene;
    vsg::ref_ptr<vsgcef::UIPanel> panel;
    vsg::ref_ptr<vsgcef::UIPanel> propertyPanel;
    vsg::ref_ptr<vsgcef::UIPanel> toolbarPanel;
    vsg::ref_ptr<vsg::ThreadSafeQueue<SceneCommand>> commands;
    vsg::ref_ptr<vsg::Camera> camera;
    std::vector<vsg::ref_ptr<vsgcef::UIPanelInputHandler>> panelInputs;
    std::vector<CubeEntry> cubes;
    int selectedCube = -1;
    uint32_t cubeCount = 0;
};

class CubeLabels : public vsg::Inherit<vsg::Command, CubeLabels>
{
public:
    explicit CubeLabels(std::shared_ptr<State> state) : state_(std::move(state)) {}

    void record(vsg::CommandBuffer&) const override
    {
        const auto* viewport = ImGui::GetMainViewport();
        auto* draw = ImGui::GetForegroundDrawList();
        for (const auto& cube : state_->cubes)
        {
            const auto clip = state_->camera->projectionMatrix->transform() *
                state_->camera->viewMatrix->transform() *
                vsg::dvec4(cube.position.x, cube.position.y, cube.position.z + 0.85, 1.0);
            if (clip.w <= 0.0) continue;
            const float x = viewport->WorkPos.x + static_cast<float>((clip.x / clip.w * 0.5 + 0.5) * viewport->WorkSize.x);
            const float y = viewport->WorkPos.y + static_cast<float>((0.5 - clip.y / clip.w * 0.5) * viewport->WorkSize.y);
            const std::string label = "Cube " + std::to_string(cube.id + 1);
            draw->AddText(ImVec2(x - 18.0f, y), IM_COL32(245, 245, 245, 255), label.c_str());
        }
    }

private:
    std::shared_ptr<State> state_;
};

vsg::ref_ptr<vsg::StateGroup> cubePipeline()
{
    const std::string dir = VKVSG_SHADER_DIR;
    auto vert = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", dir + "/equator_line.vert.spv");
    auto frag = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", dir + "/equator_line.frag.spv");
    if (!vert || !frag) return {};
    vsg::VertexInputState::Bindings bindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX}};
    vsg::VertexInputState::Attributes attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};
    auto raster = vsg::RasterizationState::create();
    raster->cullMode = VK_CULL_MODE_NONE;
    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE),
        raster, vsg::MultisampleState::create(), vsg::ColorBlendState::create(), vsg::DepthStencilState::create()};
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{},
                                               vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    auto group = vsg::StateGroup::create();
    group->add(vsg::BindGraphicsPipeline::create(
        vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vert, frag}, states)));
    return group;
}

vsg::ref_ptr<vsg::Node> cubeNode(const vsg::vec3& position, const vsg::vec4& color,
                                 vsg::ref_ptr<vsg::vec4Array>& colorsOut)
{
    const std::vector<vsg::vec3> vertices{
        {-0.45f,-0.45f,-0.45f}, {0.45f,-0.45f,-0.45f}, {0.45f,0.45f,-0.45f}, {-0.45f,0.45f,-0.45f},
        {-0.45f,-0.45f,0.45f}, {0.45f,-0.45f,0.45f}, {0.45f,0.45f,0.45f}, {-0.45f,0.45f,0.45f}};
    const std::vector<uint16_t> indices{
        4,5,6,6,7,4, 1,0,3,3,2,1, 0,4,7,7,3,0,
        5,1,2,2,6,5, 3,7,6,6,2,3, 0,1,5,5,4,0};
    auto group = cubePipeline();
    if (!group) return {};
    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(vertices.size()));
    auto colors = vsg::vec4Array::create(static_cast<uint32_t>(vertices.size()));
    colorsOut = colors;
    auto elements = vsg::ushortArray::create(static_cast<uint32_t>(indices.size()));
    for (std::size_t i = 0; i < vertices.size(); ++i) { (*positions)[i] = vertices[i]; (*colors)[i] = color; }
    for (std::size_t i = 0; i < indices.size(); ++i) (*elements)[i] = indices[i];
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{positions, colors});
    draw->assignIndices(elements);
    draw->indexCount = static_cast<uint32_t>(indices.size());
    draw->instanceCount = 1;
    auto transform = vsg::MatrixTransform::create();
    transform->matrix = vsg::translate(position);
    transform->addChild(group);
    group->addChild(draw);
    return transform;
}

std::string numberString(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

void publishSelection(State& state)
{
    const CubeEntry* selected = nullptr;
    if (state.selectedCube >= 0 && static_cast<std::size_t>(state.selectedCube) < state.cubes.size())
        selected = &state.cubes[static_cast<std::size_t>(state.selectedCube)];

    const std::string name = selected ? "cube" + std::to_string(selected->id + 1) : "none";
    const std::string selectedScript =
        "if (window.__vsgCef) window.__vsgCef.receiveState('selectedObject',{name:'" + name + "'});";
    state.cefUi->executeJavaScript("hello", selectedScript);

    std::string propertyScript = "if (window.__vsgCef) window.__vsgCef.receiveState('objectProperties',{";
    if (selected)
    {
        propertyScript += "id:" + std::to_string(selected->id) +
            ",name:'" + name + "',tx:" + numberString(selected->position.x) +
            ",ty:" + numberString(selected->position.y) +
            ",tz:" + numberString(selected->position.z);
    }
    else
    {
        propertyScript += "id:-1,name:'none',tx:0,ty:0,tz:0";
    }
    propertyScript += "});";
    state.cefUi->executeJavaScript("properties", propertyScript);
}

void updateSelection(State& state, int selected)
{
    state.selectedCube = selected;
    for (const auto& cube : state.cubes)
    {
        const bool isSelected = static_cast<int>(cube.id) == selected;
        const vsg::vec4 color = isSelected ? vsg::vec4(1.0f, 0.95f, 0.1f, 1.0f) : cube.baseColor;
        for (uint32_t i = 0; i < cube.colors->size(); ++i) (*cube.colors)[i] = color;
        cube.colors->dirty();
    }

    if (state.cefUi) publishSelection(state);
}

class CubeSelectionHandler : public vsg::Inherit<vsg::Visitor, CubeSelectionHandler>
{
public:
    explicit CubeSelectionHandler(std::shared_ptr<State> state) : state_(std::move(state)) {}

    void apply(vsg::ButtonPressEvent& event) override
    {
        if (event.button != 1 || !state_->camera) return;
        for (const auto& panelInput : state_->panelInputs)
            if (panelInput && panelInput->containsWindowPoint(event.x, event.y)) return;

        auto intersector = vsg::LineSegmentIntersector::create(*state_->camera, event.x, event.y);
        state_->scene->accept(*intersector);
        double nearest = 2.0;
        int selected = -1;
        for (const auto& intersection : intersector->intersections)
        {
            if (!intersection || intersection->ratio >= nearest) continue;
            for (const auto& node : intersection->nodePath)
            {
                for (const auto& cube : state_->cubes)
                {
                    if (node == cube.transform)
                    {
                        nearest = intersection->ratio;
                        selected = static_cast<int>(cube.id);
                        break;
                    }
                }
                if (selected >= 0) break;
            }
        }
        if (selected >= 0) updateSelection(*state_, selected);
    }

private:
    std::shared_ptr<State> state_;
};

void addCubes(State& state, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t index = state.cubeCount++;
        const vsg::vec3 position(-2.5f + (index % 6) * 1.1f, 4.0f + (index / 6) * 1.1f, 0.5f);
        const vsg::vec4 color(0.2f + (index % 3) * 0.3f, 0.5f, 0.9f, 1.0f);
        vsg::ref_ptr<vsg::vec4Array> colors;
        auto cube = cubeNode(position, color, colors);
        if (state.viewer && state.viewer->compileManager)
        {
            auto result = state.viewer->compileManager->compile(cube);
            if (result) updateViewer(*state.viewer, result);
        }
        state.scene->addChild(cube);
        state.cubes.push_back(CubeEntry{index, cube.cast<vsg::MatrixTransform>(), colors, color, position});
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        vsg::CommandLine arguments(&argc, argv);
        auto traits = vsg::WindowTraits::create(arguments);
        traits->windowTitle = "vsgCefPanelDemo";
        traits->width = 1280; traits->height = 800;

        auto state = std::make_shared<State>();
        state->commands = vsg::ThreadSafeQueue<SceneCommand>::create(vsg::ActivityStatus::create());
        auto handler = [state](const vsgcef::CefUiCommand& command, std::string& error) {
            std::cerr << "[vsgCefPanelDemo] CEF command: " << command.type
                      << " args=" << command.argsJson << std::endl;
            if (command.type == "__vsgCef.ready")
            {
                htmlui::Json readyArgs(command.argsJson.empty() ? "{}" : command.argsJson);
                const auto panel = readyArgs.string("panel");
                if (panel == "hello" || panel == "properties") publishSelection(*state);
                return true;
            }
            htmlui::Json args(command.argsJson.empty() ? "{}" : command.argsJson);
            if (command.type == "addCubes")
            {
                const uint32_t count = static_cast<uint32_t>(args.u64("count"));
                if (count == 0 || count > 1000) { error = "Enter a number from 1 to 1000."; return false; }
                state->commands->add(SceneCommand{SceneCommand::Type::AddCubes, count, 0, vsg::vec3{}});
                std::cerr << "[vsgCefPanelDemo] queued AddCubes count=" << count << std::endl;
                return true;
            }
            if (command.type == "setCubePosition")
            {
                const uint64_t id = args.u64("id", 1000000);
                if (id >= state->cubes.size()) { error = "Unknown cube."; return false; }
                const vsg::vec3 position(
                    static_cast<float>(args.number("tx")),
                    static_cast<float>(args.number("ty")),
                    static_cast<float>(args.number("tz")));
                state->commands->add(SceneCommand{SceneCommand::Type::SetCubePosition,
                                                   0, static_cast<uint32_t>(id), position});
                return true;
            }
            if (command.type == "exit")
            {
                state->commands->add(SceneCommand{SceneCommand::Type::Exit, 0, 0, vsg::vec3{}});
                return true;
            }
            error = "Unknown command: " + command.type;
            return false;
        };
        state->cefUi = vsgcef::CefUi::create(argc, argv, VSGCEF_CEF_UI_DIR, handler);
        if (!state->cefUi || !state->cefUi->initialized()) return state->cefUi ? state->cefUi->exitCode() : 1;
        state->cefUi->addSurface("hello", VSGCEF_CEF_UI_DIR "/hello.html", 360, 300);
        state->cefUi->addSurface("properties", VSGCEF_CEF_UI_DIR "/properties.html", 360, 300);
        state->cefUi->addSurface("toolbar", VSGCEF_CEF_UI_DIR "/toolbar.html", 1280, 100);
        state->cefUi->createBrowsers();

        auto window = vsg::Window::create(traits);
        if (!window) return 1;
        state->viewer = vsg::Viewer::create(); state->viewer->addWindow(window);
        const auto extent = window->extent2D();
        auto camera = vsg::Camera::create(
            vsg::Orthographic::create(-6.4, 6.4, -4.0, 4.0, 0.1, 100.0),
            vsg::LookAt::create(vsg::dvec3(0.0, -12.0, 2.0), vsg::dvec3(0.0, 2.0, 2.0), vsg::dvec3(0.0, 0.0, 1.0)),
            vsg::ViewportState::create(0, 0, extent.width, extent.height));
        state->camera = camera;
        state->scene = vsg::Group::create();
        state->panel = vsgcef::UIPanel::create(state->cefUi, "hello", 360, 300,
                                               vsg::dvec3(-5.0, 2.0, 2.5), 3.6, 3.0);
        state->propertyPanel = vsgcef::UIPanel::create(state->cefUi, "properties", 360, 300,
                                                        vsg::dvec3(1.2, 2.0, 2.5), 3.6, 3.0);
        state->toolbarPanel = vsgcef::UIPanel::create(state->cefUi, "toolbar", 1280, 100,
                                                       vsg::dvec3(-6.4, 2.0, 5.375), 12.8, 0.625);
        addCubes(*state, 3);
        // Render the CEF panel through a separate fixed camera after the world
        // scene. This makes it a screen-space overlay: trackball rotation and
        // panning affect only the cubes, never the panel.
        auto overlayCamera = vsg::Camera::create(
            vsg::Orthographic::create(-6.4, 6.4, -4.0, 4.0, 0.1, 100.0),
            vsg::LookAt::create(vsg::dvec3(0.0, -12.0, 2.0), vsg::dvec3(0.0, 2.0, 2.0), vsg::dvec3(0.0, 0.0, 1.0)),
            vsg::ViewportState::create(0, 0, extent.width, extent.height));
        auto overlayScene = vsg::Group::create();
        overlayScene->addChild(state->toolbarPanel);
        overlayScene->addChild(state->panel);
        overlayScene->addChild(state->propertyPanel);
        auto renderGraph = vsg::RenderGraph::create(window);
        auto worldView = vsg::View::create(camera);
        worldView->addChild(vsg::createHeadlight());
        worldView->addChild(state->scene);
        renderGraph->addChild(worldView);
        auto overlayView = vsg::View::create(overlayCamera);
        overlayView->addChild(overlayScene);
        renderGraph->addChild(overlayView);
        auto commandGraph = vsg::CommandGraph::create(window, renderGraph);
        state->viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        IMGUI_CHECKVERSION();
        if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
        state->viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        renderGraph->addChild(vsgImGui::RenderImGui::create(window, CubeLabels::create(state)));
        state->panelInputs.push_back(vsgcef::UIPanelInputHandler::create(state->panel, overlayCamera, extent.width, extent.height));
        state->panelInputs.push_back(vsgcef::UIPanelInputHandler::create(state->propertyPanel, overlayCamera, extent.width, extent.height));
        state->panelInputs.push_back(vsgcef::UIPanelInputHandler::create(state->toolbarPanel, overlayCamera, extent.width, extent.height, false));
        for (const auto& panelInput : state->panelInputs) state->viewer->addEventHandler(panelInput);
        state->viewer->addEventHandler(CubeSelectionHandler::create(state));
        state->viewer->addEventHandler(vsg::CloseHandler::create(state->viewer));
        auto trackball = vsg::Trackball::create(camera);
        trackball->rotateButtonMask = vsg::BUTTON_MASK_2;
        trackball->panButtonMask = vsg::BUTTON_MASK_3;
        state->viewer->addEventHandler(trackball);
        state->viewer->compile();
        updateSelection(*state, -1);
        while (state->viewer->advanceToNextFrame())
        {
            state->cefUi->doMessageLoopWork(); state->panel->update(); state->propertyPanel->update(); state->toolbarPanel->update();
            for (const auto& command : state->commands->take_all())
            {
                if (command.type == SceneCommand::Type::AddCubes)
                {
                    std::cerr << "[vsgCefPanelDemo] processing AddCubes count=" << command.count << std::endl;
                    addCubes(*state, command.count);
                    std::cerr << "[vsgCefPanelDemo] AddCubes complete total=" << state->cubeCount << std::endl;
                }
                else if (command.type == SceneCommand::Type::SetCubePosition && command.cubeId < state->cubes.size())
                {
                    auto& cube = state->cubes[command.cubeId];
                    cube.position = command.position;
                    cube.transform->matrix = vsg::translate(command.position);
                    if (state->selectedCube == static_cast<int>(command.cubeId)) publishSelection(*state);
                }
                else if (command.type == SceneCommand::Type::Exit)
                {
                    state->viewer->close();
                }
            }
            state->viewer->handleEvents(); state->viewer->update(); state->viewer->recordAndSubmit(); state->viewer->present();
        }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "[vsgCefPanelDemo] " << e.what() << std::endl; return 1; }
}
