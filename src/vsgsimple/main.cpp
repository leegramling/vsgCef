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
};

struct AppState
{
    std::vector<SceneItem> objects;
    std::shared_ptr<vsgcef::CefUi> cefUi;
    vsg::observer_ptr<vsg::Viewer> viewer;
    std::shared_ptr<htmlui::HtmlUi> htmlUi;
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

std::string objectsJson(const std::vector<SceneItem>& objects)
{
    std::ostringstream json;
    json << std::fixed << std::setprecision(3) << "[";
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        const auto& object = objects[i];
        if (i != 0) json << ",";
        json << "{"
             << "\"id\":" << object.id << ","
             << "\"name\":\"" << jsonEscape(object.name) << "\","
             << "\"type\":\"" << jsonEscape(object.type) << "\","
             << "\"position\":[" << object.position.x << "," << object.position.y << "," << object.position.z << "]"
             << "}";
    }
    json << "]";
    return json.str();
}

void publishHtmlUi(AppState& state)
{
    if (!state.htmlUi) return;
    state.htmlUi->publishDirty("objects");
    state.htmlUi->publishDirty("inspector");
}

vsg::ref_ptr<vsg::StateGroup> createPipelineStateGroup()
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

    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE),
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
    state.objects.push_back(SceneItem{1, "Blue Cube", "cube", {-2.0, -1.0, 0.55}, {0.20f, 0.52f, 0.92f, 1.0f}, {}});
    state.objects.push_back(SceneItem{2, "Gold Sphere", "sphere", {0.0, 1.2, 0.65}, {0.95f, 0.70f, 0.18f, 1.0f}, {}});
    state.objects.push_back(SceneItem{3, "Green Cube", "cube", {2.0, -0.2, 0.55}, {0.25f, 0.72f, 0.38f, 1.0f}, {}});

    for (auto& object : state.objects)
    {
        object.transform = vsg::MatrixTransform::create();
        object.transform->matrix = vsg::translate(object.position);
        object.transform->addChild(object.type == "sphere" ? createSphereNode(object.color) : createCubeNode(object.color));
        root->addChild(object.transform);
    }
    return root;
}

void renderCefPanel(AppState& state, uint32_t deviceID)
{
    if (!state.cefUi) return;

    publishHtmlUi(state);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    state.htmlUi->renderPanelImGui("objects",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x, viewport->WorkPos.y),
                                   ImVec2(320.0f, viewport->WorkSize.y));
    state.htmlUi->renderPanelImGui("inspector",
                                   state.viewer,
                                   deviceID,
                                   ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 320.0f, viewport->WorkPos.y + 24.0f),
                                   ImVec2(300.0f, 300.0f),
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
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
        windowTraits->width = 1100;
        windowTraits->height = 720;
        windowTraits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

        const int numFrames = arguments.value(-1, "-f");
        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto state = std::make_shared<AppState>();
        state->htmlUi = std::make_shared<htmlui::HtmlUi>();
        state->htmlUi->panel("objects", "CEF Objects", "cef_objects_input", vsgcef::CefSurfaceId::Primary);
        state->htmlUi->panel("inspector", "CEF Inspector", "cef_inspector_input", vsgcef::CefSurfaceId::Secondary);
        state->htmlUi->state("objects", [state] {
            return objectsJson(state->objects);
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
            std::cout << "[vsgCefSimple] renamed object " << it->id << " to \"" << it->name << "\"" << std::endl;
            return true;
        });

        auto commandHandler = [htmlUi = state->htmlUi](const vsgcef::CefUiCommand& command, std::string& errorMessage) {
            return htmlUi->handleCommand(command, errorMessage);
        };

        state->cefUi = vsgcef::CefUi::create(cefArgc, cefArgv, VSGCEF_CEF_UI_DIR, commandHandler);
        state->htmlUi->setCefUi(state->cefUi);
        if (state->cefUi && state->cefUi->exitCode() >= 0) return state->cefUi->exitCode();
        if (state->cefUi && state->cefUi->initialized())
            state->cefUi->createBrowsers();
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
        const uint32_t sceneX = std::min(320u, extent.width);
        const uint32_t sceneWidth = std::max(1u, extent.width - sceneX);
        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, -10.0, 6.0), vsg::dvec3(0.0, 0.0, 0.4), vsg::dvec3(0.0, 0.0, 1.0));
        auto perspective = vsg::Perspective::create(45.0, static_cast<double>(sceneWidth) / static_cast<double>(extent.height), 0.1, 100.0);
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(static_cast<int32_t>(sceneX), 0, sceneWidth, extent.height));

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
