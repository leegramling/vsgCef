#include "vsgcef/CefUi.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/Texture.h>
#include <vsgImGui/imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
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

struct CefTexture
{
    vsg::ref_ptr<vsg::ubvec4Array2D> imageData;
    vsg::ref_ptr<vsgImGui::Texture> texture;
    uint64_t paintCount = 0;
    bool compiled = false;
};

struct AppState
{
    std::vector<SceneItem> objects;
    std::shared_ptr<vsgcef::CefUi> cefUi;
    vsg::observer_ptr<vsg::Viewer> viewer;
    CefTexture cefTexture;
    vsgcef::CefSurfaceId focusedSurface = vsgcef::CefSurfaceId::Stats;
    bool sentInitialObjects = false;
    bool objectStateDirty = true;
    Clock::time_point lastObjectPublishTime;
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

void publishObjectsToCef(AppState& state)
{
    if (!state.cefUi) return;
    const auto snapshot = state.cefUi->statsSnapshot();
    if (!snapshot.browserCreated) return;

    const auto now = Clock::now();
    const bool retryPublish = !state.sentInitialObjects ||
        now - state.lastObjectPublishTime >= std::chrono::milliseconds(500);
    if (!state.objectStateDirty && !retryPublish) return;

    const std::string script = "if (window.vsgCefSimple) window.vsgCefSimple.receiveObjects(" + objectsJson(state.objects) + ");";
    state.cefUi->executeJavaScript(vsgcef::CefSurfaceId::Stats, script);
    state.objectStateDirty = false;
    state.sentInitialObjects = true;
    state.lastObjectPublishTime = now;
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

void updateCefTexture(AppState& state, const vsgcef::CefSurfaceFrame& frame)
{
    const auto& snapshot = frame.snapshot;
    if (!snapshot.available || snapshot.width <= 0 || snapshot.height <= 0 || frame.bgra.empty()) return;
    if (state.cefTexture.paintCount == snapshot.paintCount && state.cefTexture.texture) return;

    const auto width = static_cast<uint32_t>(snapshot.width);
    const auto height = static_cast<uint32_t>(snapshot.height);
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (frame.bgra.size() < expected) return;

    const bool needsNewTexture = !state.cefTexture.imageData ||
        state.cefTexture.imageData->width() != width ||
        state.cefTexture.imageData->height() != height;
    if (needsNewTexture)
    {
        state.cefTexture.imageData = vsg::ubvec4Array2D::create(width, height);
        state.cefTexture.imageData->properties.format = VK_FORMAT_B8G8R8A8_UNORM;
        state.cefTexture.imageData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        state.cefTexture.texture = {};
        state.cefTexture.compiled = false;
    }

    std::memcpy(reinterpret_cast<uint8_t*>(state.cefTexture.imageData->dataPointer()), frame.bgra.data(), expected);
    state.cefTexture.imageData->dirty();
    if (!state.cefTexture.texture) state.cefTexture.texture = vsgImGui::Texture::create(state.cefTexture.imageData);
    state.cefTexture.paintCount = snapshot.paintCount;
}

ImTextureID cefTextureId(AppState& state, uint32_t deviceID)
{
    if (!state.cefTexture.texture) return {};
    if (!state.cefTexture.compiled)
    {
        auto viewer = vsg::ref_ptr<vsg::Viewer>(state.viewer);
        if (!viewer || !viewer->compileManager) return {};
        auto result = viewer->compileManager->compile(state.cefTexture.texture);
        if (!result) return {};
        updateViewer(*viewer, result);
        state.cefTexture.compiled = true;
    }
    return state.cefTexture.texture->id(deviceID);
}

uint32_t cefInputModifiers()
{
    const ImGuiIO& io = ImGui::GetIO();
    uint32_t modifiers = vsgcef::CefInputModifierNone;
    if (io.KeyShift) modifiers |= vsgcef::CefInputModifierShift;
    if (io.KeyCtrl) modifiers |= vsgcef::CefInputModifierControl;
    if (io.KeyAlt) modifiers |= vsgcef::CefInputModifierAlt;
    if (io.MouseDown[ImGuiMouseButton_Left]) modifiers |= vsgcef::CefInputModifierLeftMouseButton;
    if (io.MouseDown[ImGuiMouseButton_Middle]) modifiers |= vsgcef::CefInputModifierMiddleMouseButton;
    if (io.MouseDown[ImGuiMouseButton_Right]) modifiers |= vsgcef::CefInputModifierRightMouseButton;
    return modifiers;
}

void renderCefPanel(AppState& state, uint32_t deviceID)
{
    if (!state.cefUi) return;

    publishObjectsToCef(state);
    auto frame = state.cefUi->statsFrame();
    updateCefTexture(state, frame);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320.0f, viewport->WorkSize.y), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("CEF Objects", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 surfaceSize = ImGui::GetContentRegionAvail();
        surfaceSize.x = std::max(surfaceSize.x, 1.0f);
        surfaceSize.y = std::max(surfaceSize.y, 1.0f);
        state.cefUi->resizeSurface(vsgcef::CefSurfaceId::Stats, static_cast<int>(surfaceSize.x), static_cast<int>(surfaceSize.y));

        const ImVec2 surfaceMin = ImGui::GetCursorScreenPos();
        if (ImTextureID textureId = cefTextureId(state, deviceID))
        {
            ImGui::Image(textureId, surfaceSize);
        }
        else
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(surfaceMin, ImVec2(surfaceMin.x + surfaceSize.x, surfaceMin.y + surfaceSize.y), IM_COL32(23, 27, 29, 255));
            drawList->AddText(ImVec2(surfaceMin.x + 14.0f, surfaceMin.y + 14.0f), IM_COL32(230, 236, 238, 255), "Waiting for CEF paint");
        }

        ImGui::SetCursorScreenPos(surfaceMin);
        ImGui::InvisibleButton("cef_objects_input", surfaceSize);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetMousePos();
        const int localX = std::clamp(static_cast<int>(mouse.x - surfaceMin.x), 0, static_cast<int>(surfaceSize.x) - 1);
        const int localY = std::clamp(static_cast<int>(mouse.y - surfaceMin.y), 0, static_cast<int>(surfaceSize.y) - 1);
        const int browserWidth = frame.snapshot.width > 0 ? frame.snapshot.width : static_cast<int>(surfaceSize.x);
        const int browserHeight = frame.snapshot.height > 0 ? frame.snapshot.height : static_cast<int>(surfaceSize.y);
        const int browserX = std::clamp(static_cast<int>((static_cast<float>(localX) / surfaceSize.x) * static_cast<float>(browserWidth)), 0, browserWidth - 1);
        const int browserY = std::clamp(static_cast<int>((static_cast<float>(localY) / surfaceSize.y) * static_cast<float>(browserHeight)), 0, browserHeight - 1);
        const uint32_t modifiers = cefInputModifiers();

        if (hovered)
        {
            state.cefUi->sendMouseMove(vsgcef::CefSurfaceId::Stats, browserX, browserY, modifiers);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                state.focusedSurface = vsgcef::CefSurfaceId::Stats;
                state.cefUi->setFocus(vsgcef::CefSurfaceId::Stats, true);
                state.cefUi->sendMouseClick(vsgcef::CefSurfaceId::Stats, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, false, 1);
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.cefUi->sendMouseClick(vsgcef::CefSurfaceId::Stats, browserX, browserY, modifiers, vsgcef::CefMouseButton::Left, true, 1);
            }
        }

        if (state.focusedSurface == vsgcef::CefSurfaceId::Stats)
        {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextFrameWantCaptureKeyboard(true);
            for (auto character : io.InputQueueCharacters)
            {
                if (character == 0) continue;
                const auto codepoint = static_cast<uint32_t>(character);
                state.cefUi->sendKey(vsgcef::CefSurfaceId::Stats, static_cast<int>(codepoint), modifiers, false);
                state.cefUi->sendKeyChar(vsgcef::CefSurfaceId::Stats, codepoint, modifiers);
                state.cefUi->sendKey(vsgcef::CefSurfaceId::Stats, static_cast<int>(codepoint), modifiers, true);
            }
            constexpr std::array<std::pair<ImGuiKey, int>, 12> keys{{
                {ImGuiKey_Backspace, 0x08},
                {ImGuiKey_Tab, 0x09},
                {ImGuiKey_Enter, 0x0D},
                {ImGuiKey_Escape, 0x1B},
                {ImGuiKey_Home, 0x24},
                {ImGuiKey_End, 0x23},
                {ImGuiKey_LeftArrow, 0x25},
                {ImGuiKey_UpArrow, 0x26},
                {ImGuiKey_RightArrow, 0x27},
                {ImGuiKey_DownArrow, 0x28},
                {ImGuiKey_Insert, 0x2D},
                {ImGuiKey_Delete, 0x2E}}};
            for (const auto& key : keys)
            {
                if (ImGui::IsKeyPressed(key.first, false)) state.cefUi->sendKey(vsgcef::CefSurfaceId::Stats, key.second, modifiers, false);
                if (ImGui::IsKeyReleased(key.first)) state.cefUi->sendKey(vsgcef::CefSurfaceId::Stats, key.second, modifiers, true);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
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
        auto commandHandler = [state](const vsgcef::CefUiCommand& command, std::string& errorMessage) {
            if (command.type != "renameObject")
            {
                errorMessage = "Unhandled CEF command: " + command.type;
                return false;
            }

            auto it = std::find_if(state->objects.begin(), state->objects.end(), [&](const SceneItem& object) {
                return object.id == command.objectId;
            });
            if (it == state->objects.end())
            {
                errorMessage = "Unknown object id.";
                return false;
            }
            it->name = command.name;
            state->objectStateDirty = true;
            std::cout << "[vsgCefSimple] renamed object " << it->id << " to \"" << it->name << "\"" << std::endl;
            return true;
        };

        state->cefUi = vsgcef::CefUi::create(cefArgc, cefArgv, VSGCEF_CEF_UI_DIR, commandHandler);
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
