#pragma once

// ImGui config untuk Android ARM32 + OpenGL ES2
// File ini di-include otomatis oleh imgui.h

// Nonaktifkan fitur yang tidak dibutuhkan / berat
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS

// Gunakan 16-bit index untuk hemat memori (cukup untuk UI kita)
#define ImDrawIdx unsigned short

// Nonaktifkan format string yang jarang dipakai
#define IMGUI_USE_BGRA_PACKED_COLOR

// Wchar minimal
#define IMGUI_USE_WCHAR32 0
