/**
 * libguiaml.so
 * GUI AML — ImGui overlay via eglSwapBuffers hook
 * Pure native .so, tidak butuh Lua/MoNetLoader
 * ARM32 armeabi-v7a
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

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_internal.h"

// ─────────────────────────────────────────────────────────────────────────────
#define LOG_TAG "libguiaml"
#define LOGFILE "/storage/emulated/0/guiaml_log.txt"
#define EXPORT  __attribute__((visibility("default")))

static void _log(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", buf);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

// ─────────────────────────────────────────────────────────────────────────────
//  EGL hook
// ─────────────────────────────────────────────────────────────────────────────

typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

typedef int  (*DobbyHook_t)(void*, void*, void**);
typedef void*(*DobbyResolver_t)(const char*, const char*);

// touch hook — AND_TouchEvent(action, x, y, pointerId) di libGTASA.so
typedef void (*AND_TouchEvent_t)(int action, int x, int y, int pointerId);
static AND_TouchEvent_t orig_AND_TouchEvent = nullptr;

struct WinRect { float x,y,w,h; };
static WinRect         g_win_rect   = {};
static pthread_mutex_t g_rect_mu    = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────────────────────────
//  ImGui state
// ─────────────────────────────────────────────────────────────────────────────

static bool g_init    = false;
static bool g_visible = true;

static bool touch_in_gui(float x, float y) {
    if (!g_init || !g_visible) return false;
    pthread_mutex_lock(&g_rect_mu);
    WinRect r = g_win_rect;
    pthread_mutex_unlock(&g_rect_mu);
    return (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h);
}

// EGL context tracking — untuk detect re-create saat transisi loading→world
static EGLDisplay g_last_display = EGL_NO_DISPLAY;
static EGLSurface g_last_surface = EGL_NO_SURFACE;
static EGLContext g_last_context  = EGL_NO_CONTEXT;

// Touch
struct Touch { float x, y; bool down; };
static Touch            g_touch = {};
static pthread_mutex_t  g_mu    = PTHREAD_MUTEX_INITIALIZER;

// ─────────────────────────────────────────────────────────────────────────────
//  Widget state
// ─────────────────────────────────────────────────────────────────────────────

// Tab: Player
static bool  tog_godmode   = false;
static bool  tog_noclip    = false;
static bool  tog_infammo   = false;
static bool  tog_speedhack = false;
static int   sl_health     = 100;
static int   sl_armor      = 0;
static float sl_speed      = 1.0f;
static float sl_fov        = 70.0f;
static bool  cb_esp        = false;
static bool  cb_aimbot     = false;
static bool  cb_radar      = true;
static bool  cb_hud        = true;

// Tab: Weapon
static int  combo_wep      = 0;
static int  sl_ammo        = 9999;
static bool cb_infammo_wep = false;
static const char* k_weapons[] = {
    "Fists (0)","Pistol (22)","Desert Eagle (24)",
    "Shotgun (25)","AK-47 (30)","M4 (31)",
    "MP5 (29)","Sniper (34)","RPG (35)","Minigun (38)"
};

// Tab: Teleport / Coords
static float tp_x = 0.0f, tp_y = 0.0f, tp_z = 5.0f;
static bool  cb_freeze = false;

// Tab: Visual
static float col_esp[4]  = {1.0f, 0.3f, 0.3f, 1.0f};
static float col_name[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static float sl_ui_alpha = 1.0f;
static int   radio_team  = 0;
static bool  cb_fullbright = false;
static bool  cb_nightvision = false;

// Tab: Console
static char  g_log[8192]  = "[GUIAML] Ready.\n";
static char  g_cmd[256]   = "";

static void log_ui(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    // append
    size_t rem = sizeof(g_log) - strlen(g_log) - 1;
    strncat(g_log, tmp, rem);
    rem = sizeof(g_log) - strlen(g_log) - 1;
    strncat(g_log, "\n", rem);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Tombol toggle ON/OFF berwarna
static bool ToggleBtn(const char* id, const char* lbl_on, const char* lbl_off,
                       bool* state, ImVec2 sz = ImVec2(0,0)) {
    bool hit = false;
    if (*state) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f,0.50f,0.35f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f,0.65f,0.45f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.06f,0.38f,0.26f,1));
        if (ImGui::Button(lbl_on,  sz)) { *state=false; hit=true; }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f,0.08f,0.08f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f,0.12f,0.12f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f,0.06f,0.06f,1));
        if (ImGui::Button(lbl_off, sz)) { *state=true;  hit=true; }
    }
    ImGui::PopStyleColor(3);
    return hit;
}

// Label + indicator dot
static void StatusDot(const char* label, bool on) {
    ImGui::TextColored(
        on ? ImVec4(0.2f,0.9f,0.5f,1) : ImVec4(0.5f,0.5f,0.5f,1),
        on ? "[ON]" : "[OFF]");
    ImGui::SameLine();
    ImGui::Text("%s", label);
}

// ─────────────────────────────────────────────────────────────────────────────
//  GUI Render
// ─────────────────────────────────────────────────────────────────────────────

static void hook_AND_TouchEvent(int action, int x, int y, int ptr) {
    float fx = (float)x, fy = (float)y;

    pthread_mutex_lock(&g_mu);
    g_touch.x    = fx;
    g_touch.y    = fy;
    g_touch.down = (action == 0 || action == 2); // 0=DOWN 2=MOVE
    pthread_mutex_unlock(&g_mu);

    // Block kalau touch di dalam GUI window
    if (touch_in_gui(fx, fy)) {
        _log("[TOUCH] consumed action=%d x=%d y=%d", action, x, y);
        return;
    }
    orig_AND_TouchEvent(action, x, y, ptr);
}

static void render_gui() {
    ImGuiIO& io = ImGui::GetIO();

    pthread_mutex_lock(&g_mu);
    io.MousePos     = ImVec2(g_touch.x, g_touch.y);
    io.MouseDown[0] = g_touch.down;
    pthread_mutex_unlock(&g_mu);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // ── Mini-button saat hidden ───────────────────────────────────────────
    if (!g_visible) {
        ImGui::SetNextWindowPos(ImVec2(6,6), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(80,32), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##fab", nullptr,
            ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::Button("GUIAML", ImVec2(-1,0))) g_visible = true;
        ImGui::End();
        goto done;
    }

    {
        float W = io.DisplaySize.x;
        float H = io.DisplaySize.y;

        ImGui::SetNextWindowPos(ImVec2(W*0.02f, H*0.04f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(W*0.58f, H*0.88f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(sl_ui_alpha * 0.96f);

        bool p_open = true;
        ImGui::Begin("GUI AML  |  brruham-arch", &p_open,
            ImGuiWindowFlags_NoCollapse);
        if (!p_open) g_visible = false;

        // Update rect cache untuk touch blocking
        {
            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 sz  = ImGui::GetWindowSize();
            pthread_mutex_lock(&g_rect_mu);
            g_win_rect = {pos.x, pos.y, sz.x, sz.y};
            pthread_mutex_unlock(&g_rect_mu);
        }

        // Status bar
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f,0.12f,0.10f,1));
        ImGui::BeginChild("##status", ImVec2(-1, 28), false);
        ImGui::SetCursorPosY(4);
        StatusDot("God",   tog_godmode);   ImGui::SameLine(0,10);
        StatusDot("Clip",  tog_noclip);    ImGui::SameLine(0,10);
        StatusDot("Ammo",  tog_infammo);   ImGui::SameLine(0,10);
        StatusDot("Speed", tog_speedhack);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("##T")) {

            // ══ TAB: PLAYER ══════════════════════════════════════════════
            if (ImGui::BeginTabItem("Player")) {
                ImGui::Spacing();
                ImGui::Text("Toggles"); ImGui::Separator(); ImGui::Spacing();

                float hw = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
                ImVec2 bsz(hw, 0);

                if (ToggleBtn("##gm","God Mode  ON","God Mode OFF",&tog_godmode,bsz))
                    log_ui("GodMode = %s", tog_godmode?"ON":"OFF");
                ImGui::SameLine(0,8);
                if (ToggleBtn("##nc","NoClip  ON","NoClip  OFF",&tog_noclip,bsz))
                    log_ui("NoClip = %s", tog_noclip?"ON":"OFF");

                if (ToggleBtn("##ia","InfAmmo  ON","InfAmmo  OFF",&tog_infammo,bsz))
                    log_ui("InfAmmo = %s", tog_infammo?"ON":"OFF");
                ImGui::SameLine(0,8);
                if (ToggleBtn("##sh","SpeedHack ON","SpeedHack OFF",&tog_speedhack,bsz))
                    log_ui("SpeedHack = %s", tog_speedhack?"ON":"OFF");

                ImGui::Spacing();
                ImGui::Text("Stats"); ImGui::Separator(); ImGui::Spacing();

                ImGui::Text("HP    "); ImGui::SameLine(60);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##hp",&sl_health,0,100))
                    log_ui("HP = %d", sl_health);

                ImGui::Text("Armor "); ImGui::SameLine(60);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##ar",&sl_armor,0,100))
                    log_ui("Armor = %d", sl_armor);

                ImGui::Text("Speed "); ImGui::SameLine(60);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##sp",&sl_speed,0.1f,5.0f,"%.2fx"))
                    log_ui("Speed = %.2f", sl_speed);

                ImGui::Text("FOV   "); ImGui::SameLine(60);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##fv",&sl_fov,40,120,"%.0f deg"))
                    log_ui("FOV = %.0f", sl_fov);

                ImGui::Spacing();
                ImGui::Text("Features"); ImGui::Separator(); ImGui::Spacing();

                if (ImGui::Checkbox("ESP",          &cb_esp))       log_ui("ESP = %d",   cb_esp);
                if (ImGui::Checkbox("Aimbot",       &cb_aimbot))    log_ui("Aimbot = %d",cb_aimbot);
                if (ImGui::Checkbox("Show Radar",   &cb_radar))     log_ui("Radar = %d", cb_radar);
                if (ImGui::Checkbox("Show HUD",     &cb_hud))       log_ui("HUD = %d",   cb_hud);

                ImGui::EndTabItem();
            }

            // ══ TAB: WEAPON ══════════════════════════════════════════════
            if (ImGui::BeginTabItem("Weapon")) {
                ImGui::Spacing();
                ImGui::Text("Weapon"); ImGui::Separator(); ImGui::Spacing();

                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##wep",&combo_wep,k_weapons,IM_ARRAYSIZE(k_weapons));

                ImGui::Spacing();
                float bw = ImGui::GetContentRegionAvail().x;
                if (ImGui::Button("Give Weapon",ImVec2(bw,0)))
                    log_ui("Give: %s", k_weapons[combo_wep]);
                if (ImGui::Button("Remove All Weapons",ImVec2(bw,0)))
                    log_ui("Removed all weapons");

                ImGui::Spacing();
                ImGui::Text("Ammo"); ImGui::Separator(); ImGui::Spacing();

                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##ammo",&sl_ammo,0,9999);
                if (ImGui::Checkbox("Infinite Ammo",&cb_infammo_wep))
                    log_ui("InfAmmo = %d", cb_infammo_wep);
                if (ImGui::Button("Set Ammo",ImVec2(bw,0)))
                    log_ui("Ammo set %d for %s", sl_ammo, k_weapons[combo_wep]);

                ImGui::EndTabItem();
            }

            // ══ TAB: TELEPORT ═════════════════════════════════════════════
            if (ImGui::BeginTabItem("Teleport")) {
                ImGui::Spacing();
                ImGui::Text("Coordinates"); ImGui::Separator(); ImGui::Spacing();

                ImGui::Text("X "); ImGui::SameLine(30);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##tx",&tp_x,1.0f,10.0f,"%.2f");

                ImGui::Text("Y "); ImGui::SameLine(30);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##ty",&tp_y,1.0f,10.0f,"%.2f");

                ImGui::Text("Z "); ImGui::SameLine(30);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##tz",&tp_z,0.5f,5.0f,"%.2f");

                ImGui::Spacing();
                float bw = ImGui::GetContentRegionAvail().x;
                if (ImGui::Button("Teleport", ImVec2(bw,0)))
                    log_ui("Teleport -> %.2f %.2f %.2f", tp_x, tp_y, tp_z);

                ImGui::Spacing();
                ImGui::Separator();
                if (ImGui::Checkbox("Freeze Position",&cb_freeze))
                    log_ui("Freeze = %d", cb_freeze);

                ImGui::Spacing();
                ImGui::Text("Quick TP"); ImGui::Separator(); ImGui::Spacing();

                const char* spots[] = {"Grove Street","LS Airport","Las Venturas","San Fierro"};
                float sv[4][3] = {
                    {2495.0f,-1688.0f,13.3f},
                    {-1400.0f,-200.0f,14.0f},
                    {2000.0f,1000.0f,10.0f},
                    {-1982.0f,138.0f,27.0f}
                };
                for (int i=0;i<4;i++) {
                    if (ImGui::Button(spots[i], ImVec2(bw,0))) {
                        tp_x=sv[i][0]; tp_y=sv[i][1]; tp_z=sv[i][2];
                        log_ui("Quick TP: %s", spots[i]);
                    }
                }

                ImGui::EndTabItem();
            }

            // ══ TAB: VISUAL ═══════════════════════════════════════════════
            if (ImGui::BeginTabItem("Visual")) {
                ImGui::Spacing();

                if (ImGui::Checkbox("Fullbright",   &cb_fullbright))   log_ui("Fullbright = %d",   cb_fullbright);
                if (ImGui::Checkbox("Night Vision", &cb_nightvision))  log_ui("NightVision = %d",  cb_nightvision);

                ImGui::Spacing();
                ImGui::Text("UI Alpha"); ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##uia",&sl_ui_alpha,0.2f,1.0f,"%.2f");

                ImGui::Spacing();
                ImGui::Text("ESP Color");
                ImGui::SetNextItemWidth(-1);
                ImGui::ColorEdit4("##ce",col_esp,
                    ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_PickerHueBar);

                ImGui::Text("Name Color");
                ImGui::SetNextItemWidth(-1);
                ImGui::ColorEdit4("##cn",col_name,
                    ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_PickerHueBar);

                ImGui::Spacing();
                ImGui::Text("Team"); ImGui::Separator(); ImGui::Spacing();
                ImGui::RadioButton("None",  &radio_team,0); ImGui::SameLine();
                ImGui::RadioButton("Alpha", &radio_team,1); ImGui::SameLine();
                ImGui::RadioButton("Beta",  &radio_team,2); ImGui::SameLine();
                ImGui::RadioButton("Grove", &radio_team,3);

                ImGui::EndTabItem();
            }

            // ══ TAB: CONSOLE ══════════════════════════════════════════════
            if (ImGui::BeginTabItem("Console")) {
                ImGui::Spacing();

                float log_h = ImGui::GetContentRegionAvail().y - 56;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f,0.05f,0.07f,1));
                ImGui::BeginChild("##logwnd", ImVec2(-1,log_h), true);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f,1.0f,0.75f,1));
                ImGui::TextUnformatted(g_log);
                ImGui::PopStyleColor();
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::Spacing();
                float bw = ImGui::GetContentRegionAvail().x;
                ImGui::SetNextItemWidth(bw - 60);
                bool enter = ImGui::InputText("##cons",g_cmd,sizeof(g_cmd),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Run") || enter) && g_cmd[0]) {
                    log_ui("> %s", g_cmd);
                    g_cmd[0] = '\0';
                }
                if (ImGui::Button("Clear", ImVec2(bw,0)))
                    g_log[0] = '\0';

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

done:
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ─────────────────────────────────────────────────────────────────────────────
//  ImGui init (dipanggil di frame pertama)
// ─────────────────────────────────────────────────────────────────────────────

static void do_shutdown() {
    if (g_init) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_init = false;
        _log("[GUIAML] Context destroyed, will re-init");
    }
}

static void do_init(EGLDisplay dpy, EGLSurface surf) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    EGLint w=1080, h=1920;
    eglQuerySurface(dpy, surf, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);

    float sc = (float)w / 1080.0f * 2.2f;
    io.FontGlobalScale = sc;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding  = 6;  s.FrameRounding  = 4;
    s.GrabRounding    = 3;  s.ScrollbarRounding = 4;
    s.WindowBorderSize = 1; s.FrameBorderSize   = 0;
    s.WindowPadding   = ImVec2(10,10);
    s.FramePadding    = ImVec2(6,4);
    s.ItemSpacing     = ImVec2(8,6);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.04f,0.05f,0.07f,0.96f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.07f,0.16f,0.13f,1);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.09f,0.24f,0.19f,1);
    c[ImGuiCol_Header]           = ImVec4(0.10f,0.30f,0.24f,0.8f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.14f,0.44f,0.34f,0.9f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.17f,0.52f,0.41f,1);
    c[ImGuiCol_Button]           = ImVec4(0.09f,0.26f,0.20f,0.9f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.13f,0.40f,0.30f,1);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.07f,0.20f,0.15f,1);
    c[ImGuiCol_FrameBg]          = ImVec4(0.07f,0.09f,0.11f,1);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.10f,0.16f,0.14f,1);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.18f,0.65f,0.50f,1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.22f,0.85f,0.65f,1);
    c[ImGuiCol_CheckMark]        = ImVec4(0.22f,0.85f,0.65f,1);
    c[ImGuiCol_Tab]              = ImVec4(0.07f,0.16f,0.12f,1);
    c[ImGuiCol_TabHovered]       = ImVec4(0.12f,0.36f,0.27f,1);
    c[ImGuiCol_TabActive]        = ImVec4(0.10f,0.28f,0.22f,1);
    c[ImGuiCol_Separator]        = ImVec4(0.15f,0.30f,0.24f,0.6f);

    s.ScaleAllSizes(sc);

    ImGui_ImplOpenGL3_Init("#version 100");

    g_init = true;
    _log("[GUIAML] ImGui init OK %dx%d scale=%.2f", w, h, sc);
}

// ─────────────────────────────────────────────────────────────────────────────
//  eglSwapBuffers hook
// ─────────────────────────────────────────────────────────────────────────────

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLContext ctx = eglGetCurrentContext();

    // Context berubah (transisi loading->world) = shutdown + re-init
    if (ctx != g_last_context || dpy != g_last_display || surf != g_last_surface) {
        do_shutdown();
        g_last_display = dpy;
        g_last_surface = surf;
        g_last_context = ctx;
    }

    if (!g_init && ctx != EGL_NO_CONTEXT) {
        do_init(dpy, surf);
    }

    if (g_init) {
        // Pastikan GL state bersih sebelum ImGui render
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Bind framebuffer default (0) agar render ke layar, bukan FBO game
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Update viewport sesuai surface saat ini
        EGLint w = 1080, h = 1920;
        eglQuerySurface(dpy, surf, EGL_WIDTH,  &w);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        glViewport(0, 0, w, h);

        // Update DisplaySize jika berubah (rotate, resize)
        ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);

        render_gui();

        // Restore GL state — jangan tinggalkan state kotor untuk game
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

    return orig_eglSwapBuffers(dpy, surf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API (dipanggil dari luar jika perlu, opsional)
// ─────────────────────────────────────────────────────────────────────────────

extern "C" EXPORT void guiaml_toggle()                         { g_visible = !g_visible; }
extern "C" EXPORT void guiaml_inject_touch(float x,float y,int d) {
    pthread_mutex_lock(&g_mu);
    g_touch = {x, y, d != 0};
    pthread_mutex_unlock(&g_mu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  AML entry points
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* i = "guiaml|1.0|ImGui Overlay via eglSwapBuffers|brruham-arch";
    return (void*)i;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    _log("[GUIAML] OnModPreLoad");
    g_init    = false;
    g_visible = true;
}

EXPORT void OnModLoad() {
    _log("[GUIAML] OnModLoad");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { _log("[GUIAML] ERROR: libdobby.so"); return; }

    auto hook     = (DobbyHook_t)     dlsym(hDobby, "DobbyHook");
    auto resolver = (DobbyResolver_t) dlsym(hDobby, "DobbySymbolResolver");
    if (!hook || !resolver) { _log("[GUIAML] ERROR: Dobby syms"); return; }

    // Coba resolver dulu, fallback ke dlsym
    void* addr = resolver("libEGL.so", "eglSwapBuffers");
    if (!addr) {
        void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
        if (hEGL) addr = dlsym(hEGL, "eglSwapBuffers");
    }
    if (!addr) { _log("[GUIAML] ERROR: eglSwapBuffers addr"); return; }
    _log("[GUIAML] eglSwapBuffers @ %p", addr);

    int r = hook(addr, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    if (r != 0) { _log("[GUIAML] ERROR: hook failed r=%d", r); return; }

    // ── Hook AND_TouchEvent — cari base libGTASA.so dari /proc/self/maps ──
    uintptr_t gtasa_base = 0;
    {
        FILE* maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
                    gtasa_base = (uintptr_t)strtoul(line, nullptr, 16);
                    break;
                }
            }
            fclose(maps);
        }
    }
    _log("[GUIAML] libGTASA.so base from maps = 0x%08X", (unsigned)gtasa_base);

    if (gtasa_base) {
        // offset dari nm output: 0x2697C0, Thumb2 → berikan genap ke Dobby
        void* addr_touch = (void*)(gtasa_base + 0x2697C0);
        _log("[GUIAML] AND_TouchEvent target = %p", addr_touch);
        int rt = hook(addr_touch, (void*)hook_AND_TouchEvent,
                      (void**)&orig_AND_TouchEvent);
        if (rt == 0) _log("[GUIAML] AND_TouchEvent hooked OK");
        else         _log("[GUIAML] WARN: AND_TouchEvent hook failed r=%d", rt);
    } else {
        _log("[GUIAML] WARN: libGTASA.so base not found in maps");
    }

    _log("[GUIAML] OK — GUI aktif di frame pertama");
}

} // extern "C"
