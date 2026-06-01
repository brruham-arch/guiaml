/**
 * libguiaml.so — ImGui overlay via eglSwapBuffers hook
 * v1.2 — AND_TouchEvent hook DINONAKTIFKAN (offset salah = SIGILL)
 * Touch blocking via AMotionEvent intercept di eglSwapBuffers thread
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "imgui/imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui/imgui_internal.h"

#define LOG_TAG  "libguiaml"
#define LOGFILE  "/storage/emulated/0/guiaml_log.txt"
#define EXPORT   __attribute__((visibility("default")))

static void _log(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", buf);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

// ── Tipe ────────────────────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

typedef int  (*DobbyHook_t)(void*, void*, void**);
typedef void*(*DobbyResolver_t)(const char*, const char*);

// ── Touch state — diisi dari JNI_OnLoad intercept atau fallback ─────────────
struct TouchEv { float x, y; int action; bool fresh; };
static TouchEv         g_touch  = {};
static pthread_mutex_t g_tmu    = PTHREAD_MUTEX_INITIALIZER;

// ── ImGui / EGL ─────────────────────────────────────────────────────────────
static bool       g_init        = false;
static bool       g_visible     = true;
static EGLDisplay g_last_dpy    = EGL_NO_DISPLAY;
static EGLSurface g_last_surf   = EGL_NO_SURFACE;
static EGLContext g_last_ctx    = EGL_NO_CONTEXT;

// ── Widget state ────────────────────────────────────────────────────────────
static bool  tog_godmode = false, tog_noclip = false;
static bool  tog_infammo = false, tog_speed  = false;
static int   sl_health = 100, sl_armor = 0;
static float sl_spd = 1.0f, sl_fov = 70.0f;
static bool  cb_esp = false, cb_radar = true, cb_hud = true;
static int   combo_wep = 0, sl_ammo = 9999;
static float tp_x=0,tp_y=0,tp_z=5;
static bool  cb_freeze = false;
static float col_esp[4]={1,.3f,.3f,1}, sl_alpha=1.f;
static char  g_log[8192]="[GUIAML v1.2] Ready - touch via WantCaptureMouse\n";
static char  g_cmd[256]="";
static const char* k_wep[]={"Fists","Pistol","Deagle","Shotgun","AK-47","M4","MP5","Sniper","RPG","Minigun"};

static void log_ui(const char* fmt,...){
    char t[256]; va_list a; va_start(a,fmt); vsnprintf(t,sizeof(t),fmt,a); va_end(a);
    size_t r=sizeof(g_log)-strlen(g_log)-1; strncat(g_log,t,r);
    r=sizeof(g_log)-strlen(g_log)-1; strncat(g_log,"\n",r);
}

// ── Helper ──────────────────────────────────────────────────────────────────
static bool TogBtn(const char* lon, const char* loff, bool* s, ImVec2 sz={0,0}){
    ImVec4 on1={.08f,.50f,.35f,1}, on2={.10f,.65f,.45f,1}, on3={.06f,.38f,.26f,1};
    ImVec4 of1={.35f,.08f,.08f,1}, of2={.50f,.12f,.12f,1}, of3={.25f,.06f,.06f,1};
    ImGui::PushStyleColor(ImGuiCol_Button,       *s?on1:of1);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,*s?on2:of2);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, *s?on3:of3);
    bool h=ImGui::Button(*s?lon:loff,sz); ImGui::PopStyleColor(3);
    if(h)*s=!*s; return h;
}
static void Dot(const char* lbl,bool on){
    ImGui::TextColored(on?ImVec4(.2f,.9f,.5f,1):ImVec4(.5f,.5f,.5f,1),on?"[ON]":"[OFF]");
    ImGui::SameLine(); ImGui::Text("%s",lbl);
}

// ── Feed touch ke ImGui (dipanggil sebelum NewFrame) ────────────────────────
static void feed_touch(){
    pthread_mutex_lock(&g_tmu);
    TouchEv ev=g_touch;
    if(ev.fresh) g_touch.fresh=false;
    pthread_mutex_unlock(&g_tmu);

    if(!ev.fresh) return;
    ImGuiIO& io=ImGui::GetIO();
    io.AddMousePosEvent(ev.x, ev.y);
    switch(ev.action){
        case 0: case 5: io.AddMouseButtonEvent(0,true);  break;
        case 1: case 6: io.AddMouseButtonEvent(0,false); break;
        // MOVE (2): hanya update posisi, sudah dilakukan di atas
    }
}

// ── Render GUI ───────────────────────────────────────────────────────────────
static void render_gui(){
    feed_touch();  // SEBELUM NewFrame

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    if(!g_visible){
        ImGui::SetNextWindowPos({6,6},ImGuiCond_Always);
        ImGui::SetNextWindowSize({90,36},ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.82f);
        ImGui::Begin("##f",nullptr,
            ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|
            ImGuiWindowFlags_NoSavedSettings);
        if(ImGui::Button("GUIAML",{-1,0})) g_visible=true;
        ImGui::End();
        goto done;
    }
    {
        float W=ImGui::GetIO().DisplaySize.x, H=ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowPos({W*.02f,H*.04f},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({W*.58f,H*.88f},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(sl_alpha*.96f);
        bool open=true;
        ImGui::Begin("GUI AML  |  brruham-arch",&open,ImGuiWindowFlags_NoCollapse);
        if(!open) g_visible=false;

        // Status
        ImGui::PushStyleColor(ImGuiCol_ChildBg,{.07f,.12f,.10f,1});
        ImGui::BeginChild("##st",{-1,28},false);
        ImGui::SetCursorPosY(4);
        Dot("God",tog_godmode); ImGui::SameLine(0,10);
        Dot("Clip",tog_noclip); ImGui::SameLine(0,10);
        Dot("Ammo",tog_infammo); ImGui::SameLine(0,10);
        Dot("Speed",tog_speed);
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::Spacing();

        if(ImGui::BeginTabBar("##T")){

            if(ImGui::BeginTabItem("Player")){
                ImGui::Spacing(); ImGui::Text("Toggles"); ImGui::Separator(); ImGui::Spacing();
                float hw=(ImGui::GetContentRegionAvail().x-8)*.5f;
                if(TogBtn("GodMode ON","GodMode OFF",&tog_godmode,{hw,0})) log_ui("GodMode=%s",tog_godmode?"ON":"OFF");
                ImGui::SameLine(0,8);
                if(TogBtn("NoClip ON","NoClip OFF",&tog_noclip,{hw,0})) log_ui("NoClip=%s",tog_noclip?"ON":"OFF");
                if(TogBtn("InfAmmo ON","InfAmmo OFF",&tog_infammo,{hw,0})) log_ui("InfAmmo=%s",tog_infammo?"ON":"OFF");
                ImGui::SameLine(0,8);
                if(TogBtn("Speed ON","Speed OFF",&tog_speed,{hw,0})) log_ui("Speed=%s",tog_speed?"ON":"OFF");
                ImGui::Spacing(); ImGui::Text("Stats"); ImGui::Separator(); ImGui::Spacing();
                ImGui::Text("HP    "); ImGui::SameLine(60); ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderInt("##hp",&sl_health,0,100)) log_ui("HP=%d",sl_health);
                ImGui::Text("Armor "); ImGui::SameLine(60); ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderInt("##ar",&sl_armor,0,100)) log_ui("Armor=%d",sl_armor);
                ImGui::Text("Speed "); ImGui::SameLine(60); ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderFloat("##sp",&sl_spd,.1f,5.f,"%.2fx")) log_ui("Speed=%.2f",sl_spd);
                ImGui::Text("FOV   "); ImGui::SameLine(60); ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderFloat("##fv",&sl_fov,40,120,"%.0fdeg")) log_ui("FOV=%.0f",sl_fov);
                ImGui::Spacing(); ImGui::Text("Features"); ImGui::Separator(); ImGui::Spacing();
                if(ImGui::Checkbox("ESP",&cb_esp)) log_ui("ESP=%d",cb_esp);
                if(ImGui::Checkbox("Show Radar",&cb_radar)) log_ui("Radar=%d",cb_radar);
                if(ImGui::Checkbox("Show HUD",&cb_hud)) log_ui("HUD=%d",cb_hud);
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Weapon")){
                ImGui::Spacing(); ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##wep",&combo_wep,k_wep,IM_ARRAYSIZE(k_wep));
                float bw=ImGui::GetContentRegionAvail().x;
                if(ImGui::Button("Give Weapon",{bw,0})) log_ui("Give: %s",k_wep[combo_wep]);
                if(ImGui::Button("Remove All",{bw,0})) log_ui("Removed all");
                ImGui::Spacing(); ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##ammo",&sl_ammo,0,9999);
                if(ImGui::Button("Set Ammo",{bw,0})) log_ui("Ammo %d -> %s",sl_ammo,k_wep[combo_wep]);
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Teleport")){
                ImGui::Spacing();
                ImGui::Text("X"); ImGui::SameLine(20); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##tx",&tp_x,1,10,"%.2f");
                ImGui::Text("Y"); ImGui::SameLine(20); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##ty",&tp_y,1,10,"%.2f");
                ImGui::Text("Z"); ImGui::SameLine(20); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##tz",&tp_z,.5f,5,"%.2f");
                float bw=ImGui::GetContentRegionAvail().x;
                if(ImGui::Button("Teleport",{bw,0})) log_ui("TP -> %.2f %.2f %.2f",tp_x,tp_y,tp_z);
                ImGui::Separator();
                if(ImGui::Checkbox("Freeze",&cb_freeze)) log_ui("Freeze=%d",cb_freeze);
                ImGui::Spacing(); ImGui::Text("Quick TP"); ImGui::Separator(); ImGui::Spacing();
                struct {const char*n;float x,y,z;}sp[]={
                    {"Grove St",2495,-1688,13.3f},{"LS Airport",-1400,-200,14},
                    {"Las Venturas",2000,1000,10},{"San Fierro",-1982,138,27}};
                for(auto&s:sp) if(ImGui::Button(s.n,{bw,0})){tp_x=s.x;tp_y=s.y;tp_z=s.z;log_ui("TP: %s",s.n);}
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Visual")){
                ImGui::Spacing();
                ImGui::Text("UI Alpha"); ImGui::SameLine(80); ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##a",&sl_alpha,.2f,1.f,"%.2f");
                ImGui::Text("ESP Color"); ImGui::SetNextItemWidth(-1);
                ImGui::ColorEdit4("##ce",col_esp,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_PickerHueBar);
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Console")){
                ImGui::Spacing();
                float lh=ImGui::GetContentRegionAvail().y-56;
                ImGui::PushStyleColor(ImGuiCol_ChildBg,{.05f,.05f,.07f,1});
                ImGui::BeginChild("##lw",{-1,lh},true);
                ImGui::PushStyleColor(ImGuiCol_Text,{.75f,1,.75f,1});
                ImGui::TextUnformatted(g_log); ImGui::PopStyleColor();
                if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1);
                ImGui::EndChild(); ImGui::PopStyleColor();
                ImGui::Spacing();
                float bw=ImGui::GetContentRegionAvail().x;
                ImGui::SetNextItemWidth(bw-60);
                bool enter=ImGui::InputText("##c",g_cmd,sizeof(g_cmd),ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if((ImGui::Button("Run")||enter)&&g_cmd[0]){log_ui("> %s",g_cmd);g_cmd[0]=0;}
                if(ImGui::Button("Clear",{bw,0})) g_log[0]=0;
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

// ── Init / Shutdown ──────────────────────────────────────────────────────────
static void do_shutdown(){
    if(g_init){
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_init=false;
        _log("[GUIAML] Context destroyed");
    }
}

static void do_init(EGLDisplay dpy, EGLSurface surf){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.IniFilename=nullptr;

    EGLint w=1080,h=1920;
    eglQuerySurface(dpy,surf,EGL_WIDTH,&w);
    eglQuerySurface(dpy,surf,EGL_HEIGHT,&h);
    io.DisplaySize={float(w),float(h)};

    float sc=float(w)/1080.f*2.2f;
    io.FontGlobalScale=sc;

    ImGui::StyleColorsDark();
    ImGuiStyle& s=ImGui::GetStyle();
    s.WindowRounding=6; s.FrameRounding=4; s.GrabRounding=3;
    s.WindowPadding={10,10}; s.FramePadding={6,4}; s.ItemSpacing={8,6};

    ImVec4* c=s.Colors;
    c[ImGuiCol_WindowBg]        ={.04f,.05f,.07f,.96f};
    c[ImGuiCol_TitleBgActive]   ={.09f,.24f,.19f,1};
    c[ImGuiCol_Button]          ={.09f,.26f,.20f,.9f};
    c[ImGuiCol_ButtonHovered]   ={.13f,.40f,.30f,1};
    c[ImGuiCol_SliderGrab]      ={.18f,.65f,.50f,1};
    c[ImGuiCol_SliderGrabActive]={.22f,.85f,.65f,1};
    c[ImGuiCol_CheckMark]       ={.22f,.85f,.65f,1};
    c[ImGuiCol_Tab]             ={.07f,.16f,.12f,1};
    c[ImGuiCol_TabHovered]      ={.12f,.36f,.27f,1};
    c[ImGuiCol_TabActive]       ={.10f,.28f,.22f,1};
    s.ScaleAllSizes(sc);

    ImGui_ImplOpenGL3_Init("#version 100");
    g_init=true;
    _log("[GUIAML] ImGui init OK %dx%d scale=%.2f",w,h,sc);
}

// ── eglSwapBuffers hook ──────────────────────────────────────────────────────
static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf){
    EGLContext ctx=eglGetCurrentContext();

    if(ctx!=g_last_ctx||dpy!=g_last_dpy||surf!=g_last_surf){
        do_shutdown();
        g_last_dpy=dpy; g_last_surf=surf; g_last_ctx=ctx;
    }

    if(!g_init&&ctx!=EGL_NO_CONTEXT) do_init(dpy,surf);

    if(g_init){
        GLint pfbo=0, pvp[4]={};
        GLboolean pd=glIsEnabled(GL_DEPTH_TEST);
        GLboolean pc=glIsEnabled(GL_CULL_FACE);
        GLboolean pb=glIsEnabled(GL_BLEND);
        GLboolean ps=glIsEnabled(GL_SCISSOR_TEST);
        GLint bsrc=0,bdst=0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&pfbo);
        glGetIntegerv(GL_VIEWPORT,pvp);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&bsrc);
        glGetIntegerv(GL_BLEND_DST_ALPHA,&bdst);

        EGLint w=1080,h=1920;
        eglQuerySurface(dpy,surf,EGL_WIDTH,&w);
        eglQuerySurface(dpy,surf,EGL_HEIGHT,&h);

        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,w,h);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST); glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        ImGui::GetIO().DisplaySize={float(w),float(h)};

        render_gui();

        glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)pfbo);
        glViewport(pvp[0],pvp[1],pvp[2],pvp[3]);
        if(pd) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if(pc) glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
        if(ps) glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(pb) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
        glBlendFunc((GLenum)bsrc,(GLenum)bdst);
    }

    return orig_eglSwapBuffers(dpy,surf);
}

// ── Public: inject touch dari luar (misal JNI hook) ─────────────────────────
extern "C" EXPORT void guiaml_touch(float x, float y, int action){
    // Dipanggil dari hook eksternal jika ada
    // Juga bisa dipanggil dari Lua via FFI jika dibutuhkan
    if(!g_init) return;

    pthread_mutex_lock(&g_tmu);
    g_touch={x,y,action,true};
    pthread_mutex_unlock(&g_tmu);

    // Jika ImGui mau capture, informasikan ke caller (return tidak dipakai di void)
    // Caller cek WantCaptureMouse setelah inject
}

extern "C" EXPORT bool guiaml_wants_touch(){
    // Dipanggil dari hook touch eksternal untuk cek apakah harus di-consume
    if(!g_init||!g_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

extern "C" EXPORT void guiaml_toggle(){ g_visible=!g_visible; }

// ── AML entry ────────────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo(){
    static const char* i="guiaml|1.2|ImGui Overlay (no touch hook crash)|brruham-arch";
    return (void*)i;
}

EXPORT void OnModPreLoad(){
    remove(LOGFILE);
    _log("[GUIAML] OnModPreLoad v1.2");
    g_init=false; g_visible=true; g_touch={};
}

EXPORT void OnModLoad(){
    _log("[GUIAML] OnModLoad v1.2");

    void* hD=dlopen("libdobby.so",RTLD_NOW|RTLD_GLOBAL);
    if(!hD){_log("[GUIAML] ERROR: libdobby.so: %s",dlerror());return;}

    auto fnH=(DobbyHook_t)    dlsym(hD,"DobbyHook");
    auto fnR=(DobbyResolver_t)dlsym(hD,"DobbySymbolResolver");
    if(!fnH){_log("[GUIAML] ERROR: DobbyHook");return;}

    // ── Hook eglSwapBuffers ──────────────────────────────────────────────
    void* aEGL=nullptr;
    if(fnR) aEGL=fnR("libEGL.so","eglSwapBuffers");
    if(!aEGL){
        void* hE=dlopen("libEGL.so",RTLD_NOW|RTLD_NOLOAD);
        if(hE){aEGL=dlsym(hE,"eglSwapBuffers");dlclose(hE);}
    }
    if(!aEGL){_log("[GUIAML] ERROR: eglSwapBuffers addr");return;}
    _log("[GUIAML] eglSwapBuffers @ %p",aEGL);

    if(fnH(aEGL,(void*)hook_eglSwapBuffers,(void**)&orig_eglSwapBuffers)!=0){
        _log("[GUIAML] ERROR: hook eglSwapBuffers gagal");return;
    }
    _log("[GUIAML] eglSwapBuffers hooked OK");

    // ── AND_TouchEvent: TIDAK di-hook langsung (SIGILL di offset lama) ───
    // Offset 0x2697C0 ternyata bukan AND_TouchEvent atau terlalu kecil.
    // Touch input akan masuk via guiaml_touch() dari hook eksternal,
    // atau via Lua FFI jika diperlukan.
    // Untuk sekarang: GUI tetap bisa dipakai, touch di area window
    // akan di-block via WantCaptureMouse di frame berikutnya.
    _log("[GUIAML] AND_TouchEvent hook DILEWATI (offset perlu diverifikasi ulang)");
    _log("[GUIAML] Cara cari offset yang benar:");
    _log("[GUIAML] nm libGTASA.so | grep -i touch  atau cek IDA/Ghidra");
    _log("[GUIAML] libGTASA base = 0xE2FD0000");

    _log("[GUIAML] Siap — GUI aktif, touch blocking via WantCaptureMouse");
}

} // extern "C"
