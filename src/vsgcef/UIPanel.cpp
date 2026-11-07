#include "vsgcef/UIPanel.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace vsgcef {
namespace {

vsg::dvec2 project(const vsg::Camera& camera, const vsg::dvec3& point,
                   uint32_t width, uint32_t height)
{
    const vsg::dvec4 clip = camera.projectionMatrix->transform() *
        camera.viewMatrix->transform() * vsg::dvec4(point.x, point.y, point.z, 1.0);
    if (clip.w == 0.0) return {-1.0, -1.0};
    return {(clip.x / clip.w * 0.5 + 0.5) * width,
            (0.5 - clip.y / clip.w * 0.5) * height};
}

} // namespace

UIPanel::UIPanel(std::shared_ptr<CefUi> cefUi, std::string surfaceId,
                 int width, int height, const vsg::dvec3& position,
                 double worldWidth, double worldHeight) :
    cefUi_(std::move(cefUi)), surfaceId_(std::move(surfaceId)),
    width_(width), height_(height), position_(position),
    worldWidth_(worldWidth), worldHeight_(worldHeight)
{
    imageData_ = vsg::ubvec4Array2D::create(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
    imageData_->properties.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageData_->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;

    const std::string shaderDir = VSGCEF_UI_SHADER_DIR;
    auto vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", shaderDir + "/ui_panel.vert.spv");
    auto fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", shaderDir + "/ui_panel.frag.spv");
    if (!vertexShader || !fragmentShader) return;

    vsg::DescriptorSetLayoutBindings descriptorBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);
    auto pushConstants = vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    vsg::VertexInputState::Bindings bindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}};
    vsg::VertexInputState::Attributes attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32_SFLOAT, 0}};
    auto depthState = vsg::DepthStencilState::create();
    depthState->depthTestEnable = VK_FALSE;
    depthState->depthWriteEnable = VK_FALSE;
    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE),
        vsg::RasterizationState::create(), vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(), depthState};
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstants);
    auto group = vsg::StateGroup::create();
    group->add(vsg::BindGraphicsPipeline::create(
        vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vertexShader, fragmentShader}, states)));
    auto descriptor = vsg::DescriptorImage::create(vsg::Sampler::create(), imageData_);
    group->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                                               vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{descriptor})));
    auto vertices = vsg::vec3Array::create({
        vsg::vec3(0.0f, 0.0f, 0.0f), vsg::vec3(worldWidth_, 0.0f, 0.0f),
        vsg::vec3(worldWidth_, 0.0f, worldHeight_), vsg::vec3(0.0f, 0.0f, worldHeight_)});
    auto texcoords = vsg::vec2Array::create({vsg::vec2(0.0f, 1.0f), vsg::vec2(1.0f, 1.0f), vsg::vec2(1.0f, 0.0f), vsg::vec2(0.0f, 0.0f)});
    auto indices = vsg::ushortArray::create({0, 1, 2, 2, 3, 0});
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays(vsg::DataList{vertices, texcoords});
    draw->assignIndices(indices);
    draw->indexCount = 6;
    draw->instanceCount = 1;
    group->addChild(draw);
    transform_ = vsg::MatrixTransform::create();
    transform_->matrix = vsg::translate(position_);
    transform_->addChild(group);
    addChild(transform_);
}

void UIPanel::setPosition(const vsg::dvec3& position)
{
    position_ = position;
    if (transform_) transform_->matrix = vsg::translate(position_);
}

void UIPanel::update()
{
    if (!cefUi_) return;
    const auto frame = cefUi_->surfaceFrame(surfaceId_);
    if (frame.snapshot.width != width_ || frame.snapshot.height != height_ || frame.bgra.empty() ||
        frame.snapshot.paintCount == paintCount_)
        return;
    const std::size_t bytes = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
    if (frame.bgra.size() < bytes) return;
    std::memcpy(imageData_->dataPointer(), frame.bgra.data(), bytes);
    imageData_->dirty();
    paintCount_ = frame.snapshot.paintCount;
}

void UIPanel::sendMouseMove(int x, int y)
{
    if (cefUi_) cefUi_->sendMouseMove(surfaceId_, x, y, CefInputModifierNone);
}

void UIPanel::sendMouseClick(int x, int y, bool mouseUp, int button)
{
    if (!cefUi_ || button != 1) return;
    std::cerr << "[UIPanel " << surfaceId_ << "] mouse " << (mouseUp ? "up" : "down")
              << " browser=" << x << "," << y << std::endl;
    cefUi_->sendMouseClick(surfaceId_, x, y, CefInputModifierNone,
                           CefMouseButton::Left, mouseUp, 1);
}

void UIPanel::sendKey(int key, bool keyUp)
{
    if (cefUi_) cefUi_->sendKey(surfaceId_, key, CefInputModifierNone, keyUp);
}

void UIPanel::sendKeyChar(uint32_t character)
{
    if (cefUi_) cefUi_->sendKeyChar(surfaceId_, character, CefInputModifierNone);
}

UIPanelInputHandler::UIPanelInputHandler(vsg::ref_ptr<UIPanel> panel,
                                         vsg::ref_ptr<vsg::Camera> camera,
                                         uint32_t viewportWidth, uint32_t viewportHeight,
                                         bool draggable) :
    panel_(std::move(panel)), camera_(std::move(camera)),
    viewportWidth_(viewportWidth), viewportHeight_(viewportHeight), draggable_(draggable)
{
    if (panel_ && camera_)
    {
        const auto p = panel_->position();
        const vsg::dvec3 corners[] = {
            p, p + vsg::dvec3(panel_->worldWidth(), 0.0, 0.0),
            p + vsg::dvec3(0.0, 0.0, panel_->worldHeight()),
            p + vsg::dvec3(panel_->worldWidth(), 0.0, panel_->worldHeight())};
        screenLeft_ = viewportWidth_;
        screenRight_ = 0.0;
        screenTop_ = viewportHeight_;
        screenBottom_ = 0.0;
        for (const auto& corner : corners)
        {
            const auto screen = project(*camera_, corner, viewportWidth_, viewportHeight_);
            screenLeft_ = std::min(screenLeft_, screen.x);
            screenRight_ = std::max(screenRight_, screen.x);
            screenTop_ = std::min(screenTop_, screen.y);
            screenBottom_ = std::max(screenBottom_, screen.y);
        }
    }
}

bool UIPanelInputHandler::containsWindowPoint(float x, float y) const
{
    int browserX = 0, browserY = 0;
    return browserCoordinates(x, y, browserX, browserY);
}

bool UIPanelInputHandler::browserCoordinates(float x, float y, int& browserX, int& browserY) const
{
    if (!panel_ || screenRight_ <= screenLeft_ || screenBottom_ <= screenTop_) return false;
    if (x < screenLeft_ || x > screenRight_ || y < screenTop_ || y > screenBottom_) return false;
    browserX = std::clamp(static_cast<int>((x - screenLeft_) / (screenRight_ - screenLeft_) * panel_->width()), 0, panel_->width() - 1);
    browserY = std::clamp(static_cast<int>((y - screenTop_) / (screenBottom_ - screenTop_) * panel_->height()), 0, panel_->height() - 1);
    return true;
}

bool UIPanelInputHandler::headerHit(float x, float y) const
{
    if (!draggable_) return false;
    int browserX = 0, browserY = 0;
    return browserCoordinates(x, y, browserX, browserY) && browserY < 48;
}

void UIPanelInputHandler::sendMouse(float x, float y, bool click, bool release, int button)
{
    int browserX = 0, browserY = 0;
    if (!browserCoordinates(x, y, browserX, browserY)) return;
    (void)click;
    panel_->sendMouseClick(browserX, browserY, release, button);
}

void UIPanelInputHandler::apply(vsg::MoveEvent& event)
{
    if (dragging_ && (event.mask & vsg::BUTTON_MASK_1) == 0)
    {
        std::cerr << "[UIPanel] drag reset: move arrived without left-button mask" << std::endl;
        dragging_ = false;
    }
    if (dragging_)
    {
        const double worldPerPixelX = 12.8 / static_cast<double>(viewportWidth_);
        const double worldPerPixelZ = 8.0 / static_cast<double>(viewportHeight_);
        const double dx = event.x - lastX_;
        const double dy = event.y - lastY_;
        auto position = panel_->position();
        position.x += dx * worldPerPixelX;
        position.z -= dy * worldPerPixelZ;
        panel_->setPosition(position);
        screenLeft_ += dx; screenRight_ += dx;
        screenTop_ += dy; screenBottom_ += dy;
        lastX_ = event.x; lastY_ = event.y;
        event.handled = true;
        return;
    }
    int x = 0, y = 0;
    if (!browserCoordinates(event.x, event.y, x, y)) return;
    panel_->sendMouseMove(x, y);
    event.handled = true;
}

void UIPanelInputHandler::apply(vsg::ButtonPressEvent& event)
{
    if (event.button == 1 && headerHit(event.x, event.y))
    {
        std::cerr << "[UIPanel] drag start window=" << event.x << "," << event.y << std::endl;
        dragging_ = true; lastX_ = event.x; lastY_ = event.y; event.handled = true; return;
    }
    if (containsWindowPoint(event.x, event.y))
    {
        sendMouse(event.x, event.y, true, false, event.button);
        event.handled = true;
    }
}

void UIPanelInputHandler::apply(vsg::ButtonReleaseEvent& event)
{
    if (dragging_ && event.button == 1)
    {
        std::cerr << "[UIPanel] drag end window=" << event.x << "," << event.y << std::endl;
        dragging_ = false; event.handled = true; return;
    }
    if (containsWindowPoint(event.x, event.y))
    {
        sendMouse(event.x, event.y, false, true, event.button);
        event.handled = true;
    }
}
void UIPanelInputHandler::apply(vsg::KeyPressEvent& event)
{
    const int key = cefKeyCode(event.keyBase);
    std::cerr << "[UIPanel] key down vsg=" << event.keyBase << " cef=" << key << std::endl;
    panel_->sendKey(key, false);
    if (event.keyBase >= 32 && event.keyBase <= 126)
        panel_->sendKeyChar(static_cast<uint32_t>(event.keyBase));
}

void UIPanelInputHandler::apply(vsg::KeyReleaseEvent& event)
{
    const int key = cefKeyCode(event.keyBase);
    std::cerr << "[UIPanel] key up vsg=" << event.keyBase << " cef=" << key << std::endl;
    panel_->sendKey(key, true);
}

int UIPanelInputHandler::cefKeyCode(uint32_t key)
{
    switch (key)
    {
    case vsg::KEY_BackSpace: return 0x08;
    case vsg::KEY_Tab: return 0x09;
    case vsg::KEY_Return: return 0x0D;
    case vsg::KEY_Escape: return 0x1B;
    case vsg::KEY_Delete: return 0x2E;
    case vsg::KEY_Home: return 0x24;
    case vsg::KEY_Left: return 0x25;
    case vsg::KEY_Up: return 0x26;
    case vsg::KEY_Right: return 0x27;
    case vsg::KEY_Down: return 0x28;
    case vsg::KEY_Insert: return 0x2D;
    case vsg::KEY_End: return 0x23;
    default: return static_cast<int>(key);
    }
}

} // namespace vsgcef
