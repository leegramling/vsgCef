#include "htmlui/HtmlUi.h"

#include <iostream>
#include <vector>

namespace htmlui {

void HtmlUi::setCefUi(std::shared_ptr<vsgcef::CefUi> cefUi)
{
    cefUi_ = std::move(cefUi);
}

HtmlPanel& HtmlUi::panel(const std::string& id,
                         const std::string& title,
                         const std::string& inputId,
                         vsgcef::CefSurfaceId surfaceId)
{
    auto found = panels_.find(id);
    if (found != panels_.end()) return *found->second.panel;

    PanelEntry entry;
    entry.surfaceId = surfaceId;
    entry.panel = std::make_unique<HtmlPanel>(title, inputId, surfaceId);
    auto [it, inserted] = panels_.emplace(id, std::move(entry));
    (void)inserted;
    for (const auto& stateEntry : states_) dirtyStatesByPanel_[id].insert(stateEntry.first);
    return *it->second.panel;
}

void HtmlUi::action(const std::string& name, ActionCallback callback)
{
    actions_[name] = std::move(callback);
}

void HtmlUi::state(const std::string& name, StateProducer producer)
{
    states_[name] = std::move(producer);
    markDirty(name);
}

void HtmlUi::markDirty(const std::string& stateName)
{
    for (const auto& entry : panels_) dirtyStatesByPanel_[entry.first].insert(stateName);
}

void HtmlUi::markAllDirty()
{
    for (const auto& panelEntry : panels_)
    {
        auto& dirtyStates = dirtyStatesByPanel_[panelEntry.first];
        for (const auto& stateEntry : states_) dirtyStates.insert(stateEntry.first);
    }
}

bool HtmlUi::handleCommand(const vsgcef::CefUiCommand& command, std::string& errorMessage)
{
    if (command.type == "__vsgCef.ready")
    {
        const Json args(command.argsJson.empty() ? "{}" : command.argsJson);
        const std::string panelId = args.string("panel");
        if (!panelId.empty())
        {
            auto* entry = findPanel(panelId);
            if (!entry)
            {
                errorMessage = "Unknown HTML UI panel: " + panelId;
                std::cout << "[HtmlUi] " << errorMessage << std::endl;
                return false;
            }
            entry->ready = true;
            std::cout << "[HtmlUi] panel ready: " << panelId << std::endl;
        }
        else
        {
            for (auto& entry : panels_) entry.second.ready = true;
            std::cout << "[HtmlUi] page ready" << std::endl;
        }

        ready_ = true;
        markAllDirty();
        return true;
    }

    auto it = actions_.find(command.type);
    if (it == actions_.end())
    {
        errorMessage = "Unknown HTML UI action: " + command.type;
        std::cout << "[HtmlUi] " << errorMessage << std::endl;
        return false;
    }

    Json args(command.argsJson.empty() ? "{}" : command.argsJson);
    return it->second(args, errorMessage);
}

void HtmlUi::publishDirty(const std::string& panelId)
{
    if (!ready_ || !cefUi_) return;

    auto dirtyIt = dirtyStatesByPanel_.find(panelId);
    if (dirtyIt == dirtyStatesByPanel_.end() || dirtyIt->second.empty()) return;

    std::vector<std::string> names(dirtyIt->second.begin(), dirtyIt->second.end());
    for (const auto& name : names) publish(name, panelId);
}

void HtmlUi::publish(const std::string& stateName, const std::string& panelId)
{
    if (!ready_ || !cefUi_) return;

    const auto* panelEntry = findPanel(panelId);
    if (!panelEntry || !panelEntry->ready) return;

    auto it = states_.find(stateName);
    if (it == states_.end()) return;

    const auto snapshot = panelEntry->surfaceId == vsgcef::CefSurfaceId::Primary
        ? cefUi_->primarySnapshot()
        : cefUi_->secondarySnapshot();
    if (!snapshot.browserCreated) return;

    const std::string dataJson = it->second();
    const std::string script =
        "if (window.__vsgCef) window.__vsgCef.receiveState(" +
        quoteJsString(stateName) + "," + dataJson + ");";
    cefUi_->executeJavaScript(panelEntry->surfaceId, script);
    dirtyStatesByPanel_[panelId].erase(stateName);
}

void HtmlUi::renderPanelImGui(const std::string& panelId,
                              vsg::observer_ptr<vsg::Viewer> viewer,
                              uint32_t deviceID,
                              const ImVec2& position,
                              const ImVec2& size,
                              ImGuiWindowFlags flags)
{
    auto* panelEntry = findPanel(panelId);
    if (!panelEntry || !panelEntry->panel) return;

    panelEntry->panel->renderImGui(cefUi_, viewer, deviceID, position, size, flags);
}

std::string HtmlUi::quoteJsString(const std::string& value)
{
    std::string quoted = "\"";
    for (char c : value)
    {
        switch (c)
        {
        case '\\': quoted += "\\\\"; break;
        case '"': quoted += "\\\""; break;
        case '\n': quoted += "\\n"; break;
        case '\r': quoted += "\\r"; break;
        case '\t': quoted += "\\t"; break;
        default: quoted.push_back(c); break;
        }
    }
    quoted += "\"";
    return quoted;
}

HtmlUi::PanelEntry* HtmlUi::findPanel(const std::string& panelId)
{
    auto it = panels_.find(panelId);
    return it == panels_.end() ? nullptr : &it->second;
}

const HtmlUi::PanelEntry* HtmlUi::findPanel(const std::string& panelId) const
{
    auto it = panels_.find(panelId);
    return it == panels_.end() ? nullptr : &it->second;
}

} // namespace htmlui
