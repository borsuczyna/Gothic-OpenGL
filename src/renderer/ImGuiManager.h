#pragma once

#include "VkWindow.h"
#include <windows.h>

namespace ImGuiManager {

void Init();
void Shutdown();
void PollInput();
void NewFrame();
void Render(VkCommandBuffer cmd);

bool WantCaptureMouse();
bool WantCaptureKeyboard();

void ToggleMenu();
bool IsMenuVisible();

bool HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace ImGuiManager
