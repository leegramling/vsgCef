#include "htmlui/HtmlUi.h"
#include "vsgcef/CefUi.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>
#include <vsg/threading/OperationQueue.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct AddCubesCommand { uint32_t count = 0; };

struct DemoState
{
    std::shared_ptr<htmlui::HtmlUi> htmlUi;
    std::shared_ptr<vsgcef::CefUi> cefUi;
    vsg::ref_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::Camera> camera;
    vsg::ref_ptr<vsg::Group> scene;
    vsg::ref_ptr<vsg::ThreadSafeQueue<AddCubesCommand>> commands;
    std::vector<vsg::ref_ptr<vsg::MatrixTransform>> cubes;
    std::vector<vsg::dvec3> cubePositions;
};

vsg::ref_ptr<vsg::StateGroup> pipeline()
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
    auto states = vsg::GraphicsPipelineStates{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE),
        raster, vsg::MultisampleState::create(), vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{},
                                               vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    auto group = vsg::StateGroup::create();
    group->add(vsg::BindGraphicsPipeline::create(
        vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vert, frag}, states)));
    return group;
}

vsg::ref_ptr<vsg::Node> cubeNode(float hue)
{
    const std::vector<vsg::vec3> vertices{
        {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f},  {0.5f,-0.5f,0.5f},  {0.5f,0.5f,0.5f},  {-0.5f,0.5f,0.5f}};
    const std::vector<uint16_t> indices{
        4,5,6, 6,7,4, 1,0,3, 3,2,1, 0,4,7, 7,3,0,
        5,1,2, 2,6,5, 3,7,6, 6,2,3, 0,1,5, 5,4,0};
    auto group = pipeline();
    if (!group) return {};
    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(vertices.size()));
    auto colors = vsg::vec4Array::create(static_cast<uint32_t>(vertices.size()));
    auto elements = vsg::ushortArray::create(static_cast<uint32_t>(indices.size()));
    const vsg::vec4 color{0.25f + 0.55f * hue, 0.55f, 0.95f - 0.45f * hue, 1.0f};
    for (std::size_t i = 0; i < vertices.size(); ++i) { (*positions)[i] = vertices[i]; (*colors)[i] = color; }
    for (std::size_t i = 0; i < indices.size(); ++i) (*elements)[i] = indices[i];
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{positions, colors});
    draw->assignIndices(elements);
    draw->indexCount = static_cast<uint32_t>(indices.size());
    draw->instanceCount = 1;
    group->addChild(draw);
    return group;
}

void addCubes(DemoState& state, uint32_t count)
{
    // The X/Y grid spacing is larger than the cube diameter, so cubes never overlap.
    constexpr double spacing = 1.8;
    for (uint32_t i = 0; i < count; ++i)
    {
        const std::size_t index = state.cubes.size();
        const double x = -2.5 + static_cast<double>(index % 6) * spacing;
        const double y =  1.0 - static_cast<double>(index / 6) * spacing;
        auto transform = vsg::MatrixTransform::create();
        transform->matrix = vsg::translate(x, y, 0.55);
        transform->addChild(cubeNode(static_cast<float>((index % 7) / 6.0)));
        if (state.viewer && state.viewer->compileManager)
        {
            auto result = state.viewer->compileManager->compile(transform);
            if (result) updateViewer(*state.viewer, result);
        }
        state.scene->addChild(transform);
        state.cubes.push_back(transform);
        state.cubePositions.emplace_back(x, y, 0.55);
    }
    if (state.htmlUi) state.htmlUi->markDirty("form");
}

std::string formState(const DemoState& state)
{
    return "{\"cubeCount\":" + std::to_string(state.cubes.size()) + "}";
}

class DemoGui : public vsg::Inherit<vsg::Command, DemoGui>
{
public:
    explicit DemoGui(std::shared_ptr<DemoState> state) : state_(std::move(state)) {}

    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        auto viewport = ImGui::GetMainViewport();
        if (state_->htmlUi)
        {
            state_->htmlUi->publishDirty("form");
            state_->htmlUi->renderPanelImGui("form", vsg::observer_ptr<vsg::Viewer>(state_->viewer), commandBuffer.deviceID,
                                             ImVec2(viewport->WorkPos.x + 24.0f, viewport->WorkPos.y + 24.0f),
                                             ImVec2(360.0f, 300.0f), ImGuiWindowFlags_None);
        }

        // Draw labels in the ImGui foreground layer by projecting a point above each
        // cube from world coordinates into the window.
        auto* draw = ImGui::GetForegroundDrawList();
        for (std::size_t i = 0; i < state_->cubes.size(); ++i)
        {
            const auto& p = state_->cubePositions[i];
            const vsg::dvec4 clip = state_->camera->projectionMatrix->transform() *
                state_->camera->viewMatrix->transform() * vsg::dvec4(p.x, p.y, p.z + 0.85, 1.0);
            if (clip.w <= 0.0) continue;
            const double ndcX = clip.x / clip.w;
            const double ndcY = clip.y / clip.w;
            const float x = viewport->WorkPos.x + static_cast<float>((ndcX * 0.5 + 0.5) * viewport->WorkSize.x);
            const float y = viewport->WorkPos.y + static_cast<float>((0.5 - ndcY * 0.5) * viewport->WorkSize.y);
            const std::string label = "Cube " + std::to_string(i + 1);
            draw->AddText(ImVec2(x - 18.0f, y), IM_COL32(245, 245, 245, 255), label.c_str());
        }
    }

private:
    std::shared_ptr<DemoState> state_;
};

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const int cefArgc = argc;
        char** cefArgv = argv;
        vsg::CommandLine arguments(&argc, argv);
        auto traits = vsg::WindowTraits::create(arguments);
        traits->windowTitle = "vsgCefDemo";
        traits->width = 1280;
        traits->height = 800;
        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto state = std::make_shared<DemoState>();
        state->htmlUi = std::make_shared<htmlui::HtmlUi>();
        state->commands = vsg::ThreadSafeQueue<AddCubesCommand>::create(vsg::ActivityStatus::create());
        state->htmlUi->state("form", [state] { return formState(*state); });
        state->htmlUi->action("addCubes", [state](const htmlui::Json& args, std::string& error) {
            const auto count = static_cast<uint32_t>(args.u64("count"));
            if (count == 0 || count > 1000) { error = "Enter a number from 1 to 1000."; return false; }
            state->commands->add(AddCubesCommand{count});
            return true;
        });
        auto handler = [htmlUi = state->htmlUi](const vsgcef::CefUiCommand& command, std::string& error) {
            return htmlUi->handleCommand(command, error);
        };
        state->cefUi = vsgcef::CefUi::create(cefArgc, cefArgv, VSGCEF_CEF_UI_DIR, handler);
        state->htmlUi->setCefUi(state->cefUi);
        if (state->cefUi && state->cefUi->exitCode() >= 0) return state->cefUi->exitCode();
        if (!state->cefUi || !state->cefUi->initialized()) return 1;
        state->htmlUi->panel("form", "CEF HTML Form", "cef_demo_form_input", VSGCEF_CEF_UI_DIR "/index.html", 360, 300);
        state->cefUi->createBrowsers();

        state->scene = vsg::Group::create();
        auto window = vsg::Window::create(traits);
        if (!window) return 1;
        state->viewer = vsg::Viewer::create();
        state->viewer->addWindow(window);
        auto extent = window->extent2D();
        state->camera = vsg::Camera::create(vsg::Perspective::create(45.0,
            static_cast<double>(extent.width) / extent.height, 0.1, 100.0),
            vsg::LookAt::create(vsg::dvec3(0.0, -11.0, 7.0), vsg::dvec3(0.0, 0.0, 0.5), vsg::dvec3(0.0, 0.0, 1.0)),
            vsg::ViewportState::create(0, 0, extent.width, extent.height));
        auto view = vsg::View::create(state->camera);
        view->addChild(vsg::createHeadlight());
        view->addChild(state->scene);
        state->viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        state->viewer->addEventHandler(vsg::CloseHandler::create(state->viewer));
        auto renderGraph = vsg::RenderGraph::create(window, view);
        renderGraph->clearValues[0].color = vsg::sRGB_to_linear(0.08f, 0.09f, 0.12f, 1.0f);
        IMGUI_CHECKVERSION();
        if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
        ImGui::GetIO().MouseDrawCursor = true;
        renderGraph->addChild(vsgImGui::RenderImGui::create(window, DemoGui::create(state)));
        auto commandGraph = vsg::CommandGraph::create(window);
        commandGraph->addChild(renderGraph);
        state->viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        auto resourceHints = vsg::ResourceHints::create();
        resourceHints->numDescriptorSets = 128;
        resourceHints->descriptorPoolSizes.push_back(
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128});
        state->viewer->compile(resourceHints);
        // Start with a few objects so the VSG scene is visible before the first
        // HTML command is submitted.
        addCubes(*state, 3);

        while (state->viewer->advanceToNextFrame())
        {
            state->cefUi->doMessageLoopWork();
            for (const auto& command : state->commands->take_all()) addCubes(*state, command.count);
            state->viewer->handleEvents();
            state->viewer->update();
            state->viewer->recordAndSubmit();
            state->viewer->present();
        }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "[vsgCefDemo] " << e.what() << std::endl; return 1; }
}
