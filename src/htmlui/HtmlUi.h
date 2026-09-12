#pragma once

#include "htmlui/HtmlPanel.h"
#include "htmlui/Json.h"
#include "vsgcef/CefUi.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace htmlui {

class HtmlUi
{
public:
    using ActionCallback = std::function<bool(const Json& args, std::string& errorMessage)>;
    using StateProducer = std::function<std::string()>;

    void setCefUi(std::shared_ptr<vsgcef::CefUi> cefUi);

    HtmlPanel& panel(const std::string& id,
                     const std::string& title,
                     const std::string& inputId,
                     const std::string& htmlFile,
                     int width = 300,
                     int height = 600);

    void action(const std::string& name, ActionCallback callback);
    void state(const std::string& name, StateProducer producer);
    void markDirty(const std::string& stateName);
    void markAllDirty();

    bool handleCommand(const vsgcef::CefUiCommand& command, std::string& errorMessage);
    void publishDirty(const std::string& panelId);
    void publish(const std::string& stateName, const std::string& panelId);
    void renderPanelImGui(const std::string& panelId,
                          vsg::observer_ptr<vsg::Viewer> viewer,
                          uint32_t deviceID,
                          const ImVec2& position,
                          const ImVec2& size,
                          ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoSavedSettings);

    bool ready() const { return ready_; }

private:
    struct PanelEntry
    {
        std::unique_ptr<HtmlPanel> panel;
        std::string surfaceId;
        bool ready = false;
    };

    static std::string quoteJsString(const std::string& value);
    PanelEntry* findPanel(const std::string& panelId);
    const PanelEntry* findPanel(const std::string& panelId) const;

    std::shared_ptr<vsgcef::CefUi> cefUi_;
    std::unordered_map<std::string, PanelEntry> panels_;
    std::unordered_map<std::string, ActionCallback> actions_;
    std::unordered_map<std::string, StateProducer> states_;
    std::unordered_map<std::string, std::unordered_set<std::string>> dirtyStatesByPanel_;
    bool ready_ = false;
};

} // namespace htmlui
