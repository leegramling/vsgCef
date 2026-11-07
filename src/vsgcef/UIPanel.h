#pragma once

#include "vsgcef/CefUi.h"

#include <memory>
#include <string>

#include <vsg/all.h>

namespace vsgcef {

// A small VSG-native CEF surface: the browser's BGRA frame is used as the
// image on a textured quad. No ImGui widget is involved.
class UIPanel : public vsg::Inherit<vsg::Group, UIPanel>
{
public:
    UIPanel(std::shared_ptr<CefUi> cefUi, std::string surfaceId,
            int width, int height, const vsg::dvec3& position,
            double worldWidth, double worldHeight);

    void update();
    void setPosition(const vsg::dvec3& position);
    void sendMouseMove(int x, int y);
    void sendMouseClick(int x, int y, bool mouseUp, int button);
    void sendKey(int key, bool keyUp);
    void sendKeyChar(uint32_t character);

    const vsg::dvec3& position() const { return position_; }
    double worldWidth() const { return worldWidth_; }
    double worldHeight() const { return worldHeight_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    std::shared_ptr<CefUi> cefUi_;
    std::string surfaceId_;
    int width_ = 0;
    int height_ = 0;
    vsg::dvec3 position_;
    double worldWidth_ = 0.0;
    double worldHeight_ = 0.0;
    vsg::ref_ptr<vsg::ubvec4Array2D> imageData_;
    vsg::ref_ptr<vsg::MatrixTransform> transform_;
    uint64_t paintCount_ = 0;
};

class UIPanelInputHandler : public vsg::Inherit<vsg::Visitor, UIPanelInputHandler>
{
public:
    UIPanelInputHandler(vsg::ref_ptr<UIPanel> panel,
                        vsg::ref_ptr<vsg::Camera> camera,
                        uint32_t viewportWidth, uint32_t viewportHeight);

    void apply(vsg::MoveEvent& event) override;
    void apply(vsg::ButtonPressEvent& event) override;
    void apply(vsg::ButtonReleaseEvent& event) override;
    void apply(vsg::KeyPressEvent& event) override;
    void apply(vsg::KeyReleaseEvent& event) override;

    bool containsWindowPoint(float x, float y) const;

private:
    bool browserCoordinates(float x, float y, int& browserX, int& browserY) const;
    void sendMouse(float x, float y, bool click, bool release, int button);
    static int cefKeyCode(uint32_t key);
    bool headerHit(float x, float y) const;

    vsg::ref_ptr<UIPanel> panel_;
    vsg::ref_ptr<vsg::Camera> camera_;
    uint32_t viewportWidth_ = 0;
    uint32_t viewportHeight_ = 0;
    double screenLeft_ = 0.0;
    double screenRight_ = 0.0;
    double screenTop_ = 0.0;
    double screenBottom_ = 0.0;
    bool dragging_ = false;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
};

} // namespace vsgcef
