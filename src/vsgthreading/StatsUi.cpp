#include "vsgthreading/StatsUi.h"

#include <vsgImGui/imgui.h>

namespace vsgthreading {

StatsUi::StatsUi(std::shared_ptr<AppData> appData) :
    appData_(std::move(appData))
{
}

void StatsUi::init()
{
}

void StatsUi::render(const FrameData& frameData)
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("vsgCef Stats")) return;

    ImGui::Text("Render FPS %.1f", frameData.renderFps);
    ImGui::Text("Simulation FPS %.1f", frameData.simulationFps);
    ImGui::Text("Simulation frame %llu", static_cast<unsigned long long>(frameData.simulationFrame));
    ImGui::Separator();
    ImGui::Text("Objects %llu", static_cast<unsigned long long>(frameData.totalObjects));
    ImGui::Text("Cubes %llu  Spheres %llu",
                static_cast<unsigned long long>(frameData.cubeCount),
                static_cast<unsigned long long>(frameData.sphereCount));
    ImGui::Text("Created %llu  Updated %llu  Removed %llu",
                static_cast<unsigned long long>(frameData.createdThisFrame),
                static_cast<unsigned long long>(frameData.updatedThisFrame),
                static_cast<unsigned long long>(frameData.removedThisFrame));
    ImGui::Text("Collisions %llu", static_cast<unsigned long long>(frameData.collisionCount));
    ImGui::Text("App events %zu  Worker backlog %zu", frameData.pendingAppEvents, frameData.workerBacklog);
    ImGui::Separator();

    if (ImGui::Checkbox("Pause simulation", &paused_) && appData_)
    {
        appData_->publishEvent(SetPausedEvent{paused_});
    }
    if (ImGui::SliderFloat("Spawn rate", &spawnRate_, 0.0f, 20.0f, "%.1f/s") && appData_)
    {
        appData_->publishEvent(SetSpawnRateEvent{static_cast<double>(spawnRate_)});
    }
    if (ImGui::Button("Spawn burst") && appData_)
    {
        appData_->publishEvent(SpawnBurstEvent{8});
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear") && appData_)
    {
        appData_->publishEvent(ClearObjectsEvent{});
    }

    ImGui::End();
}

} // namespace vsgthreading
