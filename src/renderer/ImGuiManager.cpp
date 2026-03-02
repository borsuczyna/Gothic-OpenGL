#include "ImGuiManager.h"
#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_vulkan.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../gothic/Gothic.h"
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ImGuiManager {

static bool s_initialized = false;
static bool s_menuVisible = false;
static bool s_f11WasDown  = false;

static int s_resolutionIndex = 0;
static const int RESOLUTION_COUNT = 5;
static const int s_resolutions[RESOLUTION_COUNT][2] = {
    {800, 600},
    {1024, 768},
    {1280, 720},
    {1600, 900},
    {1920, 1080},
};

static void ShowSystemCursor() {
    while (::ShowCursor(TRUE) < 0) {}
    ClipCursor(nullptr);
}

static void HideSystemCursor() {
    while (::ShowCursor(FALSE) >= 0) {}
}

static void ApplyResolution(int w, int h) {
    HWND hwnd = GVulkan_GetHWND();
    if (!hwnd) return;

    RECT r = {0, 0, w, h};
    DWORD style = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
    AdjustWindowRect(&r, style, FALSE);

    SetWindowPos(hwnd, nullptr, 0, 0,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

static int FindClosestResolution() {
    int ww = GVulkan_GetWindowWidth();
    int wh = GVulkan_GetWindowHeight();
    int best = 0;
    int bestDist = 999999;
    for (int i = 0; i < RESOLUTION_COUNT; i++) {
        int dx = s_resolutions[i][0] - ww;
        int dy = s_resolutions[i][1] - wh;
        int dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return best;
}

void Init() {
    if (s_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.Alpha = 0.95f;

    ImGui_ImplWin32_Init(GVulkan_GetHWND());

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = GVulkan_GetInstance();
    initInfo.PhysicalDevice = GVulkan_GetPhysicalDevice();
    initInfo.Device = GVulkan_GetDevice();
    initInfo.QueueFamily = GVulkan_GetGraphicsFamily();
    initInfo.Queue = GVulkan_GetGraphicsQueue();
    initInfo.DescriptorPoolSize = 16;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 2;
    initInfo.PipelineInfoMain.RenderPass = GVulkan_GetRenderPass();
    initInfo.PipelineInfoMain.Subpass = 0;

    ImGui_ImplVulkan_Init(&initInfo);

    s_resolutionIndex = FindClosestResolution();
    s_initialized = true;
}

void Shutdown() {
    if (!s_initialized) return;

    if (s_menuVisible) HideSystemCursor();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    s_initialized = false;
}

void PollInput() {
    bool f11Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f11Down && !s_f11WasDown)
        ToggleMenu();
    s_f11WasDown = f11Down;
}

void NewFrame() {
    if (!s_initialized || !s_menuVisible) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    HWND hwnd = GVulkan_GetHWND();
    POINT pt;
    if (hwnd && ::GetCursorPos(&pt) && ::ScreenToClient(hwnd, &pt))
        io.AddMousePosEvent((float)pt.x, (float)pt.y);

    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);

    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GVulkan Debug", &s_menuVisible)) {
        ImGui::Text("Window: %dx%d", GVulkan_GetWindowWidth(), GVulkan_GetWindowHeight());
        ImGui::Text("Game:   %dx%d", GVulkan_GetGameWidth(), GVulkan_GetGameHeight());
        ImGui::Separator();

        // --- Gothic Game State ---
        if (ImGui::CollapsingHeader("Gothic Game State", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& gothic = Gothic::Game::Get();
            if (gothic.IsInGame()) {
                auto timer = Gothic::Game::GetTimerInfo();
                ImGui::Text("FPS: %.1f  dt: %.1fms", Gothic::Game::GetFPS(), timer.frameTimeMs);
                ImGui::Text("Total: %.1fs  Paused: %s", timer.totalTimeSec,
                            Gothic::Game::IsPaused() ? "Yes" : "No");

                auto player = Gothic::Game::GetPlayerPosition();
                if (player.valid) {
                    ImGui::Text("Player: (%.0f, %.0f, %.0f)", player.pos.x, player.pos.y, player.pos.z);
                }

                auto cam = Gothic::Game::GetCameraInfo();
                if (cam.valid) {
                    ImGui::Text("Camera: near=%.1f far=%.0f", cam.nearPlane, cam.farPlane);
                    if (cam.screenFadeEnabled) ImGui::Text("  Screen Fade ON");
                    if (cam.cinemaScopeEnabled) ImGui::Text("  Cinema Scope ON");
                }

                auto sky = Gothic::Game::GetSkyInfo();
                if (sky.valid) {
                    ImGui::Text("Time: %.2fh  %s", sky.masterTime * 24.0f,
                                Gothic::Game::IsNight() ? "(Night)" : "(Day)");
                    ImGui::Text("Weather: %s  Rain: %.0f%%",
                                sky.weather == Gothic::WEATHER_SNOW ? "Snow" : "Rain",
                                sky.rainWeight * 100.0f);
                    ImGui::Text("Fog: (%.0f,%.0f,%.0f) dist=%.0f",
                                sky.masterState.fogColor.x, sky.masterState.fogColor.y,
                                sky.masterState.fogColor.z, sky.masterState.fogDist);
                    auto sun = Gothic::Game::GetSunDirection();
                    ImGui::Text("Sun dir: (%.2f, %.2f, %.2f)", sun.x, sun.y, sun.z);
                }
            } else {
                ImGui::TextDisabled("Not in game yet");
            }
        }
        ImGui::Separator();

        ImGui::Text("Resolution");
        int prev = s_resolutionIndex;
        ImGui::SliderInt("##res", &s_resolutionIndex, 0, RESOLUTION_COUNT - 1,
                         "%d");

        char label[64];
        snprintf(label, sizeof(label), "%dx%d",
                 s_resolutions[s_resolutionIndex][0],
                 s_resolutions[s_resolutionIndex][1]);
        ImGui::SameLine();
        ImGui::Text("%s", label);

        if (s_resolutionIndex != prev) {
            ApplyResolution(s_resolutions[s_resolutionIndex][0],
                            s_resolutions[s_resolutionIndex][1]);
        }
    }
    ImGui::End();

    if (!s_menuVisible)
        HideSystemCursor();

    ImGui::Render();
}

void Render(VkCommandBuffer cmd) {
    if (!s_initialized || !s_menuVisible) return;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
        ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
}

bool WantCaptureMouse() {
    if (!s_initialized || !s_menuVisible) return false;
    return true;
}

bool WantCaptureKeyboard() {
    if (!s_initialized || !s_menuVisible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ToggleMenu() {
    s_menuVisible = !s_menuVisible;
    if (s_menuVisible) {
        if (s_initialized) s_resolutionIndex = FindClosestResolution();
        ShowSystemCursor();
        HWND hwnd = GVulkan_GetHWND();
        if (hwnd) {
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
        }
    } else {
        HideSystemCursor();
    }
}

bool IsMenuVisible() {
    return s_menuVisible;
}

bool HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!s_initialized) return false;

    if (s_menuVisible) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return true;
        }
    }

    return false;
}

} // namespace ImGuiManager
