/**
 * libdevshell.so
 * eglSwapBuffers hook + Dear ImGui overlay
 * ARM32 / armeabi-v7a
 * Author: brruham-arch
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

// ── ImGui ─────────────────────────────────────────────────────────────────
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"

// ── AML ───────────────────────────────────────────────────────────────────
// Minimal extern "C" entry points — tidak pakai MYMOD macro agar tidak
// konflik dengan setup ImGui yang butuh static initializer sendiri

#define LOG_TAG  "libdevshell"
#define LOGFILE  "/storage/emulated/0/devshell_log.txt"
#define EXPORT   __attribute__((visibility("default")))

static void _log(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", buf);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

// ═══════════════════════════════════════════════════════════════════════════
//  STATE GLOBAL
// ═══════════════════════════════════════════════════════════════════════════

static bool g_visible       = true;   // toggle panel
static bool g_imgui_ready   = false;
static bool g_imgui_init    = false;

// EGL originals
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLDisplay g_egl_display = EGL_NO_DISPLAY;
static EGLSurface g_egl_surface = EGL_NO_SURFACE;

// Touch state (inject ke ImGui IO)
struct TouchPoint { float x, y; bool down; };
static TouchPoint g_touch = {0, 0, false};
static pthread_mutex_t g_touch_mutex = PTHREAD_MUTEX_INITIALIZER;

// ── Dobby hook types ───────────────────────────────────────────────────────
typedef int  (*DobbyHook_t)(void*, void*, void**);
typedef void*(*DobbySymbolResolver_t)(const char*, const char*);

static DobbyHook_t           g_DobbyHook     = nullptr;
static DobbySymbolResolver_t g_DobbyResolver = nullptr;

// ═══════════════════════════════════════════════════════════════════════════
//  WIDGET STATE — semua widget demo
// ═══════════════════════════════════════════════════════════════════════════

// Checkboxes
static bool cb_feature_a   = false;
static bool cb_feature_b   = true;
static bool cb_feature_c   = false;
static bool cb_godmode      = false;
static bool cb_speedhack    = false;

// Sliders
static float sl_speed       = 1.0f;
static float sl_fov         = 70.0f;
static float sl_alpha       = 1.0f;
static int   sl_health      = 100;
static int   sl_armor       = 0;

// Toggle buttons (on/off state)
static bool toggle_esp      = false;
static bool toggle_aimbot   = false;
static bool toggle_noclip   = false;
static bool toggle_inf_ammo = false;

// Input text buffers
static char input_cmd[256]  = "";
static char input_msg[512]  = "Hello SA-MP!";
static char input_ip[128]   = "127.0.0.1";
static int  input_port      = 7777;

// Combo
static int  combo_weapon    = 0;
static const char* weapons[] = {
    "Fists", "Pistol", "Desert Eagle", "Shotgun",
    "AK-47", "M4", "Sniper Rifle", "RPG", "Minigun"
};

// Color picker
static float color_pick[4]  = {0.2f, 0.8f, 0.4f, 1.0f};

// Radio
static int  radio_team      = 0;

// Output log
static char g_log_buf[4096] = "[DevShell] Ready.\n";

static void append_log(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    strncat(g_log_buf, tmp, sizeof(g_log_buf) - strlen(g_log_buf) - 1);
    strncat(g_log_buf, "\n", sizeof(g_log_buf) - strlen(g_log_buf) - 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IMGUI INIT & RENDER
// ═══════════════════════════════════════════════════════════════════════════

static void imgui_init(EGLDisplay dpy, EGLSurface surf) {
    if (g_imgui_init) return;
    g_imgui_init = true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // tidak bikin file imgui.ini

    // Dapatkan ukuran surface dari EGL
    EGLint w = 1080, h = 1920;
    eglQuerySurface(dpy, surf, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);

    // Scale UI — SA-MP Mobile biasanya 1080p
    float scale = (float)w / 1080.0f * 2.2f;
    io.FontGlobalScale = scale;

    // Style — dark theme custom
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);

    // Warna accent cyan-green
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.05f, 0.05f, 0.08f, 0.95f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.18f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.10f, 0.28f, 0.22f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.12f, 0.35f, 0.28f, 0.80f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.15f, 0.48f, 0.38f, 0.90f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.18f, 0.55f, 0.44f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.10f, 0.30f, 0.24f, 0.90f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.14f, 0.44f, 0.34f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.08f, 0.22f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.12f, 0.18f, 0.16f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.20f, 0.70f, 0.55f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 0.90f, 0.70f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.25f, 0.90f, 0.70f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(0.18f, 0.35f, 0.28f, 0.60f);
    c[ImGuiCol_Tab]              = ImVec4(0.08f, 0.18f, 0.14f, 1.00f);
    c[ImGuiCol_TabHovered]       = ImVec4(0.14f, 0.40f, 0.30f, 1.00f);
    c[ImGuiCol_TabActive]        = ImVec4(0.12f, 0.32f, 0.25f, 1.00f);

    style.ScaleAllSizes(scale);

    ImGui_ImplOpenGL3_Init("#version 100");

    g_imgui_ready = true;
    g_egl_display = dpy;
    g_egl_surface = surf;

    _log("[DevShell] ImGui init OK, display=%dx%d scale=%.2f", w, h, scale);
}

// ── Helper: tombol toggle berwarna ────────────────────────────────────────
static bool ToggleButton(const char* label_on, const char* label_off,
                          bool* state, ImVec2 size = ImVec2(0, 0)) {
    bool clicked = false;
    if (*state) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.55f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.70f, 0.52f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.40f, 0.30f, 1.0f));
        if (ImGui::Button(label_on, size)) { *state = false; clicked = true; }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.22f, 0.08f, 0.08f, 1.0f));
        if (ImGui::Button(label_off, size)) { *state = true; clicked = true; }
    }
    ImGui::PopStyleColor(3);
    return clicked;
}

// ── Render seluruh GUI ────────────────────────────────────────────────────
static void render_gui() {
    // Update touch ke ImGui IO
    ImGuiIO& io = ImGui::GetIO();
    pthread_mutex_lock(&g_touch_mutex);
    io.MousePos     = ImVec2(g_touch.x, g_touch.y);
    io.MouseDown[0] = g_touch.down;
    pthread_mutex_unlock(&g_touch_mutex);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    if (!g_visible) {
        // Panel kecil show button saat hidden
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(90, 36), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##show", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
        if (ImGui::Button("DevShell", ImVec2(-1, 0))) g_visible = true;
        ImGui::End();
        goto render_end;
    }

    {
        ImGuiIO& io2 = ImGui::GetIO();
        float W = io2.DisplaySize.x;
        float H = io2.DisplaySize.y;

        ImGui::SetNextWindowPos(ImVec2(W * 0.03f, H * 0.05f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(W * 0.60f, H * 0.85f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.95f);

        bool open = true;
        ImGui::Begin("DevShell  v1.0  |  brruham-arch", &open,
            ImGuiWindowFlags_NoCollapse);

        if (!open) { g_visible = false; }

        // ── Tab bar ──────────────────────────────────────────────────────
        if (ImGui::BeginTabBar("##tabs")) {

            // ┌─ Tab: PLAYER ──────────────────────────────────────────────
            if (ImGui::BeginTabItem("Player")) {
                ImGui::Spacing();

                // Tombol toggle baris
                ImGui::Text("Quick Toggles");
                ImGui::Separator();
                ImGui::Spacing();

                ImVec2 tbtn(ImGui::GetContentRegionAvail().x * 0.47f, 0);

                if (ToggleButton("ESP   [ON]", "ESP  [OFF]", &toggle_esp, tbtn)) {
                    append_log("ESP = %s", toggle_esp ? "ON" : "OFF");
                }
                ImGui::SameLine();
                if (ToggleButton("Aimbot [ON]", "Aimbot [OFF]", &toggle_aimbot, tbtn)) {
                    append_log("Aimbot = %s", toggle_aimbot ? "ON" : "OFF");
                }

                if (ToggleButton("NoClip [ON]", "NoClip [OFF]", &toggle_noclip, tbtn)) {
                    append_log("NoClip = %s", toggle_noclip ? "ON" : "OFF");
                }
                ImGui::SameLine();
                if (ToggleButton("InfAmmo [ON]", "InfAmmo [OFF]", &toggle_inf_ammo, tbtn)) {
                    append_log("InfAmmo = %s", toggle_inf_ammo ? "ON" : "OFF");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Stats");
                ImGui::Spacing();

                ImGui::Text("Health:"); ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##health", &sl_health, 0, 100)) {
                    append_log("Health set %d", sl_health);
                }

                ImGui::Text("Armor:"); ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##armor", &sl_armor, 0, 100)) {
                    append_log("Armor set %d", sl_armor);
                }

                ImGui::Text("Speed:"); ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##speed", &sl_speed, 0.1f, 5.0f, "%.2fx")) {
                    append_log("Speed %.2f", sl_speed);
                }

                ImGui::Text("FOV:"); ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##fov", &sl_fov, 40.0f, 120.0f, "%.0f deg")) {
                    append_log("FOV %.0f", sl_fov);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Checkboxes");
                ImGui::Spacing();

                if (ImGui::Checkbox("God Mode",   &cb_godmode))   append_log("GodMode = %d",   cb_godmode);
                if (ImGui::Checkbox("Speed Hack", &cb_speedhack)) append_log("SpeedHack = %d", cb_speedhack);
                if (ImGui::Checkbox("Feature A",  &cb_feature_a)) append_log("FeatureA = %d",  cb_feature_a);
                if (ImGui::Checkbox("Feature B",  &cb_feature_b)) append_log("FeatureB = %d",  cb_feature_b);
                if (ImGui::Checkbox("Feature C",  &cb_feature_c)) append_log("FeatureC = %d",  cb_feature_c);

                ImGui::EndTabItem();
            }

            // ┌─ Tab: WEAPON ──────────────────────────────────────────────
            if (ImGui::BeginTabItem("Weapon")) {
                ImGui::Spacing();
                ImGui::Text("Select Weapon");
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##weapon", &combo_weapon, weapons,
                             IM_ARRAYSIZE(weapons));

                ImGui::Spacing();
                ImGui::Text("Selected: %s (id=%d)", weapons[combo_weapon], combo_weapon);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                float bw = ImGui::GetContentRegionAvail().x;
                if (ImGui::Button("Give Weapon", ImVec2(bw, 0))) {
                    append_log("Give weapon: %s", weapons[combo_weapon]);
                }
                if (ImGui::Button("Remove All Weapons", ImVec2(bw, 0))) {
                    append_log("Remove all weapons");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Ammo");

                static int ammo_count = 9999;
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##ammo", &ammo_count, 0, 9999);
                if (ImGui::Button("Set Ammo", ImVec2(bw, 0))) {
                    append_log("Set ammo: %d", ammo_count);
                }

                ImGui::EndTabItem();
            }

            // ┌─ Tab: NETWORK ─────────────────────────────────────────────
            if (ImGui::BeginTabItem("Network")) {
                ImGui::Spacing();
                ImGui::Text("Server Info");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("IP Address:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##ip", input_ip, sizeof(input_ip));

                ImGui::Text("Port:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##port", &input_port);

                ImGui::Spacing();
                float bw = ImGui::GetContentRegionAvail().x;
                if (ImGui::Button("Connect", ImVec2(bw * 0.49f, 0))) {
                    append_log("Connect -> %s:%d", input_ip, input_port);
                }
                ImGui::SameLine();
                if (ImGui::Button("Disconnect", ImVec2(-1, 0))) {
                    append_log("Disconnect");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Chat");
                ImGui::Spacing();

                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextMultiline("##msg", input_msg, sizeof(input_msg),
                    ImVec2(-1, 80));

                if (ImGui::Button("Send Chat", ImVec2(-1, 0))) {
                    append_log("Chat: %s", input_msg);
                }

                ImGui::EndTabItem();
            }

            // ┌─ Tab: VISUAL ──────────────────────────────────────────────
            if (ImGui::BeginTabItem("Visual")) {
                ImGui::Spacing();
                ImGui::Text("UI Alpha");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##alpha", &sl_alpha, 0.1f, 1.0f, "%.2f")) {
                    // Terapkan alpha ke semua window
                }

                ImGui::Spacing();
                ImGui::Text("Accent Color");
                ImGui::SetNextItemWidth(-1);
                ImGui::ColorEdit4("##color", color_pick,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Team");
                ImGui::Spacing();
                ImGui::RadioButton("None",  &radio_team, 0); ImGui::SameLine();
                ImGui::RadioButton("Alpha", &radio_team, 1); ImGui::SameLine();
                ImGui::RadioButton("Beta",  &radio_team, 2); ImGui::SameLine();
                ImGui::RadioButton("Grove", &radio_team, 3);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Progress (demo)");
                static float prog = 0.0f;
                prog += 0.002f;
                if (prog > 1.0f) prog = 0.0f;
                ImGui::ProgressBar(prog, ImVec2(-1, 0));

                ImGui::EndTabItem();
            }

            // ┌─ Tab: CONSOLE ─────────────────────────────────────────────
            if (ImGui::BeginTabItem("Console")) {
                ImGui::Spacing();

                // Log output
                float log_h = ImGui::GetContentRegionAvail().y - 60;
                ImGui::BeginChild("##log", ImVec2(-1, log_h), true);
                ImGui::TextUnformatted(g_log_buf);
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();

                ImGui::Spacing();
                float bw = ImGui::GetContentRegionAvail().x;
                ImGui::SetNextItemWidth(bw - 70);
                bool enter = ImGui::InputText("##cmd", input_cmd, sizeof(input_cmd),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if (ImGui::Button("Send") || enter) {
                    if (input_cmd[0] != '\0') {
                        append_log("> %s", input_cmd);
                        input_cmd[0] = '\0';
                    }
                }
                if (ImGui::Button("Clear Log", ImVec2(-1, 0))) {
                    g_log_buf[0] = '\0';
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

render_end:
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ═══════════════════════════════════════════════════════════════════════════
//  HOOK: eglSwapBuffers
// ═══════════════════════════════════════════════════════════════════════════

static EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!g_imgui_init) {
        imgui_init(display, surface);
    }

    if (g_imgui_ready) {
        render_gui();
    }

    return orig_eglSwapBuffers(display, surface);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TOUCH INPUT HOOK
//  Hook eglSwapBuffers sudah cukup untuk render.
//  Touch kita inject manual via polling /dev/input atau hook ANativeActivity.
//  Untuk kesederhanaan, kita sediakan fungsi inject yang bisa dipanggil
//  dari Lua bridge jika diperlukan.
// ═══════════════════════════════════════════════════════════════════════════

extern "C" EXPORT void devshell_inject_touch(float x, float y, int down) {
    pthread_mutex_lock(&g_touch_mutex);
    g_touch.x    = x;
    g_touch.y    = y;
    g_touch.down = (down != 0);
    pthread_mutex_unlock(&g_touch_mutex);
}

extern "C" EXPORT void devshell_toggle() {
    g_visible = !g_visible;
}

extern "C" EXPORT int devshell_is_visible() {
    return g_visible ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  AML ENTRY POINTS
// ═══════════════════════════════════════════════════════════════════════════

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "devshell|1.0|ImGui Overlay DevShell|brruham-arch";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    _log("[DevShell] OnModPreLoad");
    g_imgui_ready = false;
    g_imgui_init  = false;
    g_visible     = true;
}

EXPORT void OnModLoad() {
    _log("[DevShell] OnModLoad start");

    // ── Load Dobby ────────────────────────────────────────────────────────
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) {
        _log("[DevShell] ERROR: libdobby.so not found");
        return;
    }

    g_DobbyHook     = (DobbyHook_t)          dlsym(hDobby, "DobbyHook");
    g_DobbyResolver = (DobbySymbolResolver_t) dlsym(hDobby, "DobbySymbolResolver");

    if (!g_DobbyHook || !g_DobbyResolver) {
        _log("[DevShell] ERROR: Dobby symbols missing");
        return;
    }
    _log("[DevShell] Dobby OK");

    // ── Resolve eglSwapBuffers dari libEGL.so ─────────────────────────────
    void* addr = g_DobbyResolver("libEGL.so", "eglSwapBuffers");
    if (!addr) {
        // Fallback: dlsym langsung
        void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
        if (hEGL) addr = dlsym(hEGL, "eglSwapBuffers");
    }

    if (!addr) {
        _log("[DevShell] ERROR: eglSwapBuffers not found");
        return;
    }
    _log("[DevShell] eglSwapBuffers addr=%p", addr);

    // ── Hook eglSwapBuffers ───────────────────────────────────────────────
    int ret = g_DobbyHook(addr,
                          (void*)hook_eglSwapBuffers,
                          (void**)&orig_eglSwapBuffers);
    if (ret != 0) {
        _log("[DevShell] ERROR: DobbyHook eglSwapBuffers failed (ret=%d)", ret);
        return;
    }
    _log("[DevShell] eglSwapBuffers hooked OK");

    _log("[DevShell] OnModLoad DONE — GUI akan muncul di frame pertama");
}

} // extern "C"
