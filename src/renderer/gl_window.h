#pragma once

#include <windows.h>

void GOpenGL_OnSetWindow(HWND hwnd);
void GOpenGL_OnPresent();
void GOpenGL_StopOpenGL();

void GOpenGL_SetGameResolution(int w, int h);
bool GOpenGL_AcquireContext();

HDC  GOpenGL_GetDC();
HWND GOpenGL_GetHWND();
int  GOpenGL_GetWindowWidth();
int  GOpenGL_GetWindowHeight();
int  GOpenGL_GetGameWidth();
int  GOpenGL_GetGameHeight();
