#include "htmlui/HtmlUi.h"
#include "vsgcef/CefUi.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/Texture.h>
#include <vsgImGui/imgui.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct SceneItem
{
    uint64_t id = 0;
    std::string name;
    std::string type;
    vsg::dvec3 position;
    vsg::vec4 color;
    vsg::ref_ptr<vsg::MatrixTransform> transform;
    vsg::dvec3 rotation;
    vsg::ref_ptr<vsg::Switch> outlineSwitch;
};

struct AppState
{
    std::vector<SceneItem> objects;
    std::shared_ptr<vsgcef::CefUi> cefUi;
    vsg::observer_ptr<vsg::Viewer> viewer;
    std::shared_ptr<htmlui::HtmlUi> htmlUi;
    uint64_t selectedObjectId = 0;
    double sceneFps = 0.0;
    double cefFps = 0.0;
    double cefPaintFps = 0.0;
    Clock::time_point metricsTime;
    uint64_t metricsFrameCount = 0;
    uint64_t metricsPaintCount = 0;
};

std::string jsonEscape(const std::string& value)
{
    std::ostringstream escaped;
    for (char c : value)
    {
        switch (c)
        {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << c; break;
        }
    }
    return escaped.str();
}

std::string objectsJson(const AppState& state)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(3) << "[";
    for (std::size_t i = 0; i < state.objects.size(); ++i)
    {
        const auto& object = state.objects[i];
        if (i != 0) json << ",";
        json << "{"
             << "\"id\":" << object.id << ","
             << "\"name\":\"" << jsonEscape(object.name) << "\","
             << "\"type\":\"" << jsonEscape(object.type) << "\","
             << "\"position\":[" << object.position.x << "," << object.position.y << "," << object.position.z << "],"
             << "\"rotation\":[" << object.rotation.x << "," << object.rotation.y << "," << object.rotation.z << "],"
             << "\"selected\":" << (object.id == state.selectedObjectId ? "true" : "false")
             << "}";
    }
    json << "]";
    return json.str();
}

std::string selectedObjectJson(const AppState& state)
{
    const auto selected = std::find_if(state.objects.begin(), state.objects.end(), [&](const SceneItem& object) {
        return object.id == state.selectedObjectId;
    });
    if (selected == state.objects.end()) return "null";

    std::ostringstream json;
    json << std::fixed << std::setprecision(3)
         << "{\"id\":" << selected->id
         << ",\"name\":\"" << jsonEscape(selected->name)
         << "\",\"type\":\"" << jsonEscape(selected->type)
         << "\",\"position\":[" << selected->position.x << "," << selected->position.y << "," << selected->position.z << "]"
         << ",\"rotation\":[" << selected->rotation.x << "," << selected->rotation.y << "," << selected->rotation.z << "]}";
    return json.str();
}

std::string renderStatusJson(const AppState& state)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(2)
         << "{\"sceneFps\":" << state.sceneFps
         << ",\"cefFps\":" << state.cefFps
         << ",\"cefPaintFps\":" << state.cefPaintFps
         << ",\"cefPaints\":" << state.metricsPaintCount
         << ",\"objectCount\":" << state.objects.size()
         << ",\"selectedObjectId\":" << state.selectedObjectId
         << ",\"selectedObject\":";
    const auto selected = std::find_if(state.objects.begin(), state.objects.end(), [&](const SceneItem& object) {
        return object.id == state.selectedObjectId;
    });
    if (selected == state.objects.end()) json << "null";
    else json << "\"" << jsonEscape(selected->name) << "\"";
    json << "}";
    return json.str();
}

void publishHtmlUi(AppState& state)
{
    if (!state.htmlUi) return;
    state.htmlUi->publishDirty("objects");
    state.htmlUi->publishDirty("property-editor");
    state.htmlUi->publishDirty("settings");
    state.htmlUi->publishDirty("renderStatus");
    state.htmlUi->publishDirty("selection");
    state.htmlUi->publishDirty("robot-configurator");
}

void updatePerformance(AppState& state)
{
    const auto now = Clock::now();
    if (state.metricsTime == Clock::time_point{}) state.metricsTime = now;
    ++state.metricsFrameCount;

    vsgcef::CefSurfaceSnapshot cefSnapshot;
    if (state.cefUi) cefSnapshot = state.cefUi->surfaceSnapshot("objects");
    const uint64_t paintCount = cefSnapshot.paintCount;
    const auto elapsed = std::chrono::duration<double>(now - state.metricsTime).count();
    if (elapsed < 0.5) return;

    state.sceneFps = static_cast<double>(state.metricsFrameCount) / elapsed;
    const uint64_t paintDelta = paintCount >= state.metricsPaintCount ? paintCount - state.metricsPaintCount : 0;
    state.cefFps = cefSnapshot.browserCreated ? 30.0 : 0.0;
    state.cefPaintFps = static_cast<double>(paintDelta) / elapsed;
    state.metricsFrameCount = 0;
    state.metricsPaintCount = paintCount;
    state.metricsTime = now;
    if (state.htmlUi) state.htmlUi->markDirty("renderStatus");
}

vsg::ref_ptr<vsg::StateGroup> createPipelineStateGroup(bool outline = false)
{
    const std::string vertPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.vert.spv";
    const std::string fragPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.frag.spv";
    auto vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vertPath);
    auto fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragPath);
    if (!vertexShader || !fragmentShader)
    {
        std::cerr << "[vsgCefSimple] Failed to load shaders from " << VKVSG_SHADER_DIR << std::endl;
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

    auto inputAssembly = vsg::InputAssemblyState::create(outline ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
    if (outline)
    {
        rasterizationState->lineWidth = 2.5f;
        rasterizationState->depthBiasEnable = VK_TRUE;
        rasterizationState->depthBiasConstantFactor = -1.0f;
        rasterizationState->depthBiasSlopeFactor = -1.0f;
    }
    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        inputAssembly,
        rasterizationState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    auto pipeline = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vertexShader, fragmentShader}, states);

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
    return stateGroup;
}

vsg::ref_ptr<vsg::Node> createIndexedGeometry(const std::vector<vsg::vec3>& vertexValues,
                                              const std::vector<uint16_t>& indexValues,
                                              const vsg::vec4& color,
                                              bool outline = false)
{
    auto stateGroup = createPipelineStateGroup(outline);
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

vsg::ref_ptr<vsg::Node> createSelectionOutline()
{
    const std::vector<vsg::vec3> vertices{
        {-0.56f, -0.56f, -0.56f}, {0.56f, -0.56f, -0.56f}, {0.56f, 0.56f, -0.56f}, {-0.56f, 0.56f, -0.56f},
        {-0.56f, -0.56f, 0.56f},  {0.56f, -0.56f, 0.56f},  {0.56f, 0.56f, 0.56f},  {-0.56f, 0.56f, 0.56f}};
    const std::vector<uint16_t> edges{
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7};
    return createIndexedGeometry(vertices, edges, vsg::vec4(1.0f, 0.82f, 0.05f, 1.0f), true);
}

vsg::ref_ptr<vsg::Node> createCubeNode(const vsg::vec4& color)
{
    std::vector<vsg::vec3> vertices{
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f}};
    std::vector<uint16_t> indices{
        4, 5, 6, 6, 7, 4, 1, 0, 3, 3, 2, 1, 0, 4, 7, 7, 3, 0,
        5, 1, 2, 2, 6, 5, 3, 7, 6, 6, 2, 3, 0, 1, 5, 5, 4, 0};
    return createIndexedGeometry(vertices, indices, color);
}

vsg::ref_ptr<vsg::Node> createFloorNode()
{
    std::vector<vsg::vec3> vertices{
        {-8.0f, -8.0f, 0.0f}, {8.0f, -8.0f, 0.0f}, {8.0f, 8.0f, 0.0f}, {-8.0f, 8.0f, 0.0f}};
    return createIndexedGeometry(vertices, {0, 1, 2, 2, 3, 0}, vsg::vec4(0.28f, 0.33f, 0.29f, 1.0f));
}

vsg::ref_ptr<vsg::Node> createSphereNode(const vsg::vec4& color)
{
    constexpr uint32_t rings = 10;
    constexpr uint32_t sectors = 20;
    std::vector<vsg::vec3> vertices;
    std::vector<uint16_t> indices;

    for (uint32_t r = 0; r <= rings; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(rings);
        const double phi = (v - 0.5) * vsg::PI;
        const double z = std::sin(phi) * 0.5;
        const double ringRadius = std::cos(phi) * 0.5;
        for (uint32_t s = 0; s < sectors; ++s)
        {
            const double theta = (static_cast<double>(s) / static_cast<double>(sectors)) * 2.0 * vsg::PI;
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

vsg::ref_ptr<vsg::Node> createScene(AppState& state)
{
    auto root = vsg::Group::create();
    root->addChild(createFloorNode());

    state.objects.clear();
    state.objects.push_back(SceneItem{1, "Blue Cube", "cube", {-2.0, -1.0, 0.55}, {0.20f, 0.52f, 0.92f, 1.0f}, {}, {}, {}});
    state.objects.push_back(SceneItem{2, "Gold Sphere", "sphere", {0.0, 1.2, 0.65}, {0.95f, 0.70f, 0.18f, 1.0f}, {}, {}, {}});
    state.objects.push_back(SceneItem{3, "Green Cube", "cube", {2.0, -0.2, 0.55}, {0.25f, 0.72f, 0.38f, 1.0f}, {}, {}, {}});

    for (auto& object : state.objects)
    {
        object.transform = vsg::MatrixTransform::create();
        object.transform->matrix = vsg::translate(object.position);
        object.transform->addChild(object.type == "sphere" ? createSphereNode(object.color) : createCubeNode(object.color));
        object.outlineSwitch = vsg::Switch::create();
        object.outlineSwitch->addChild(false, createSelectionOutline());
        object.transform->addChild(object.outlineSwitch);
        root->addChild(object.transform);
    }
    return root;
}

void updateSelection(AppState& state, uint64_t selectedObjectId)
{
    state.selectedObjectId = selectedObjectId;
    for (auto& object : state.objects)
    {
        if (object.transform)
        {
            const double scale = object.id == selectedObjectId ? 1.12 : 1.0;
            object.transform->matrix = vsg::translate(object.position) *
                vsg::rotate(vsg::radians(object.rotation.x), 1.0, 0.0, 0.0) *
                vsg::rotate(vsg::radians(object.rotation.y), 0.0, 1.0, 0.0) *
                vsg::rotate(vsg::radians(object.rotation.z), 0.0, 0.0, 1.0) *
                vsg::scale(scale, scale, scale);
            if (object.outlineSwitch) object.outlineSwitch->setAllChildren(object.id == selectedObjectId);
        }
    }
    if (state.htmlUi)
    {
        state.htmlUi->markDirty("objects");
        state.htmlUi->markDirty("renderStatus");
        state.htmlUi->markDirty("selection");
    }
}

class SceneSelectionHandler : public vsg::Inherit<vsg::Visitor, SceneSelectionHandler>
{
public:
    SceneSelectionHandler(std::shared_ptr<AppState> state,
                          vsg::ref_ptr<vsg::Node> scene,
                          vsg::ref_ptr<vsg::Camera> camera) :
        state_(std::move(state)),
        scene_(std::move(scene)),
        camera_(std::move(camera))
    {
    }

    void apply(vsg::ButtonPressEvent& event) override
    {
        if (!state_ || !scene_ || !camera_ || event.button != 1) return;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;

        auto intersector = vsg::LineSegmentIntersector::create(*camera_, event.x, event.y);
        scene_->accept(*intersector);
        const vsg::LineSegmentIntersector::Intersection* nearest = nullptr;
        for (const auto& intersection : intersector->intersections)
        {
            if (intersection && (!nearest || intersection->ratio < nearest->ratio)) nearest = intersection.get();
        }

        uint64_t selectedId = 0;
        if (nearest)
        {
            for (const auto* node : nearest->nodePath)
            {
                for (const auto& object : state_->objects)
                {
                    if (object.transform.get() == node)
                    {
                        selectedId = object.id;
                        break;
                    }
                }
                if (selectedId != 0) break;
            }
        }
        updateSelection(*state_, selectedId);
    }

private:
    std::shared_ptr<AppState> state_;
    vsg::ref_ptr<vsg::Node> scene_;
    vsg::ref_ptr<vsg::Camera> camera_;
};

void renderCefPanel(AppState& state, uint32_t deviceID)
{
    if (!state.cefUi) return;

    publishHtmlUi(state);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    state.htmlUi->renderPanelImGui("objects",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x, viewport->WorkPos.y),
                                   ImVec2(320.0f, viewport->WorkSize.y),
                                   ImGuiWindowFlags_None);
    state.htmlUi->renderPanelImGui("property-editor",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 420.0f, viewport->WorkPos.y + 24.0f),
                                   ImVec2(400.0f, 620.0f),
                                   ImGuiWindowFlags_None);
    state.htmlUi->renderPanelImGui("settings",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 420.0f, viewport->WorkPos.y + 660.0f),
                                   ImVec2(400.0f, 360.0f),
                                   ImGuiWindowFlags_None);
    state.htmlUi->renderPanelImGui("robot-configurator",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - 720.0f) * 0.5f, viewport->WorkPos.y + 36.0f),
                                   ImVec2(700.0f, viewport->WorkSize.y - 72.0f),
                                   ImGuiWindowFlags_None);
}

class SimpleGuiCommand : public vsg::Inherit<vsg::Command, SimpleGuiCommand>
{
public:
    explicit SimpleGuiCommand(std::shared_ptr<AppState> state) :
        state_(std::move(state))
    {
    }

    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                {
                    auto viewer = vsg::ref_ptr<vsg::Viewer>(state_->viewer);
                    if (viewer) viewer->close();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        renderCefPanel(*state_, commandBuffer.deviceID);
    }

private:
    std::shared_ptr<AppState> state_;
};

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const int cefArgc = argc;
        char** cefArgv = argv;

        vsg::CommandLine arguments(&argc, argv);
        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vsgCefSimple";
        windowTraits->width = 1650;
        windowTraits->height = 1080;
        windowTraits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

        const int numFrames = arguments.value(-1, "-f");
        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto state = std::make_shared<AppState>();
        state->htmlUi = std::make_shared<htmlui::HtmlUi>();
        state->htmlUi->state("objects", [state] {
            return objectsJson(*state);
        });
        state->htmlUi->state("renderStatus", [state] {
            return renderStatusJson(*state);
        });
        state->htmlUi->state("selection", [state] {
            return selectedObjectJson(*state);
        });
        state->htmlUi->action("object.select", [state](const htmlui::Json& args, std::string& errorMessage) {
            const uint64_t objectId = args.u64("id");
            if (objectId != 0 && std::none_of(state->objects.begin(), state->objects.end(), [&](const SceneItem& object) {
                    return object.id == objectId;
                }))
            {
                errorMessage = "Unknown object id.";
                return false;
            }
            updateSelection(*state, objectId);
            return true;
        });
        state->htmlUi->action("object.rename", [state](const htmlui::Json& args, std::string& errorMessage) {
            const uint64_t objectId = args.u64("id");
            const std::string name = args.string("name");
            if (name.empty())
            {
                errorMessage = "Object name cannot be empty.";
                return false;
            }

            auto it = std::find_if(state->objects.begin(), state->objects.end(), [&](const SceneItem& object) {
                return object.id == objectId;
            });
            if (it == state->objects.end())
            {
                errorMessage = "Unknown object id.";
                return false;
            }
            it->name = name;
            state->htmlUi->markDirty("objects");
            if (state->selectedObjectId == it->id) state->htmlUi->markDirty("selection");
            return true;
        });
        state->htmlUi->action("object.setTransform", [state](const htmlui::Json& args, std::string& errorMessage) {
            const uint64_t objectId = args.u64("id");
            const std::string axis = args.string("axis");
            const double value = args.number("value");
            auto it = std::find_if(state->objects.begin(), state->objects.end(), [&](const SceneItem& object) {
                return object.id == objectId;
            });
            if (it == state->objects.end())
            {
                errorMessage = "Unknown object id.";
                return false;
            }
            if (axis == "tx") it->position.x = value;
            else if (axis == "ty") it->position.y = value;
            else if (axis == "tz") it->position.z = value;
            else if (axis == "rx") it->rotation.x = value;
            else if (axis == "ry") it->rotation.y = value;
            else if (axis == "rz") it->rotation.z = value;
            else
            {
                errorMessage = "Unknown transform axis.";
                return false;
            }
            updateSelection(*state, state->selectedObjectId);
            state->htmlUi->markDirty("objects");
            state->htmlUi->markDirty("selection");
            return true;
        });

        auto commandHandler = [htmlUi = state->htmlUi](const vsgcef::CefUiCommand& command, std::string& errorMessage) {
            return htmlUi->handleCommand(command, errorMessage);
        };

        state->cefUi = vsgcef::CefUi::create(cefArgc, cefArgv, VSGCEF_CEF_UI_DIR, commandHandler);
        state->htmlUi->setCefUi(state->cefUi);
        if (state->cefUi && state->cefUi->exitCode() >= 0) return state->cefUi->exitCode();
        if (state->cefUi && state->cefUi->initialized())
        {
            state->htmlUi->panel("objects", "Outliner", "cef_objects_input", VSGCEF_CEF_UI_DIR "/../webui/dist/outliner.html", 300, 800);
            state->htmlUi->panel("property-editor", "Property Editor", "cef_property_editor_input", VSGCEF_CEF_UI_DIR "/../webui/dist/property-editor.html", 400, 620);
            state->htmlUi->panel("settings", "Render Status", "cef_settings_input", VSGCEF_CEF_UI_DIR "/../webui/dist/render-status.html", 400, 360);
            state->htmlUi->panel("robot-configurator", "Add Ocean Robot", "cef_robot_configurator_input", VSGCEF_CEF_UI_DIR "/../webui/dist/robot-configurator.html", 700, 760);
            state->cefUi->createBrowsers();
        }
        else
            state->cefUi.reset();

        auto scene = createScene(*state);
        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cerr << "[vsgCefSimple] Could not create VSG window." << std::endl;
            return 1;
        }

        auto viewer = vsg::Viewer::create();
        state->viewer = viewer;
        viewer->addWindow(window);

        const auto extent = window->extent2D();
        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, -10.0, 6.0), vsg::dvec3(0.0, 0.0, 0.4), vsg::dvec3(0.0, 0.0, 1.0));
        auto perspective = vsg::Perspective::create(45.0, static_cast<double>(extent.width) / static_cast<double>(extent.height), 0.1, 100.0);
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(0, 0, extent.width, extent.height));

        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));
        viewer->addEventHandler(SceneSelectionHandler::create(state, scene, camera));
        viewer->addEventHandler(vsg::Trackball::create(camera));

        auto view = vsg::View::create(camera);
        view->addChild(vsg::createHeadlight());
        view->addChild(scene);

        auto renderGraph = vsg::RenderGraph::create(window, view);
        renderGraph->clearValues[0].color = vsg::sRGB_to_linear(0.10f, 0.11f, 0.12f, 1.0f);

        IMGUI_CHECKVERSION();
        if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
        ImGui::GetIO().MouseDrawCursor = true;
        renderGraph->addChild(vsgImGui::RenderImGui::create(window, SimpleGuiCommand::create(state)));

        auto commandGraph = vsg::CommandGraph::create(window);
        commandGraph->addChild(renderGraph);
        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

        auto resourceHints = vsg::ResourceHints::create();
        resourceHints->numDescriptorSets = 128;
        resourceHints->descriptorPoolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128});
        viewer->compile(resourceHints);

        int framesRemaining = numFrames;
        while (viewer->advanceToNextFrame() && (numFrames < 0 || framesRemaining-- > 0))
        {
            if (state->cefUi) state->cefUi->doMessageLoopWork();
            updatePerformance(*state);
            viewer->handleEvents();
            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();
        }
    }
    catch (const vsg::Exception& exception)
    {
        std::cerr << "[vsgCefSimple] VSG exception: " << exception.message << " result = " << exception.result << std::endl;
        return 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[vsgCefSimple] exception: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
