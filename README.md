# libdevshell

**ImGui overlay mod untuk SA-MP Mobile (ARM32)**  
Render GUI langsung via `eglSwapBuffers` hook — tidak butuh MoNetLoader, tidak butuh permission overlay.

---

## Isi Panel

| Tab | Widget |
|---|---|
| **Player** | Toggle buttons (ESP, Aimbot, NoClip, InfAmmo), SliderInt (HP/Armor), SliderFloat (Speed/FOV), Checkboxes |
| **Weapon** | Combo box senjata, SliderInt ammo, Buttons |
| **Network** | InputText IP/Port, InputInt, InputTextMultiline chat, Buttons |
| **Visual** | SliderFloat alpha, ColorEdit4, RadioButton team, ProgressBar |
| **Console** | Log output, InputText + Send, Clear |

---

## Build

### Via GitHub Actions (recommended)
```bash
git add . && git commit -m "build" && git push
gh run watch $(gh run list --limit 1 --json databaseId -q '.[0].databaseId')
gh run download ... -n libdevshell-arm32 -D ./output/
```

### Via Termux (quick test)
```bash
source ~/.bashrc
cd ~/libdevshell
$NDK/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./jni/Android.mk
# Output: libs/armeabi-v7a/libdevshell.so
```

**CATATAN**: ImGui source tidak disertakan di repo.  
GitHub Actions akan download otomatis dari release ImGui v1.89.9.  
Untuk build Termux, download manual:
```bash
wget https://github.com/ocornut/imgui/archive/refs/tags/v1.89.9.tar.gz
tar xf v1.89.9.tar.gz
cp imgui-1.89.9/imgui*.{h,cpp} include/imgui/
cp imgui-1.89.9/backends/imgui_impl_opengl3* include/imgui/
```

---

## Install

```bash
# Copy ke direktori AML mods game
cp libs/armeabi-v7a/libdevshell.so \
  /storage/emulated/0/Android/data/com.sampmobilerp.game/mods/

# atau via rish
rish -c "cp /storage/emulated/0/libdevshell.so \
  /data/data/com.sampmobilerp.game/lib/libdevshell.so"
```

---

## Lua Bridge (opsional)

Copy `devshell_bridge.lua` ke folder MoNetLoader:
```
/Android/media/com.sampmobilerp.game/monetloader/devshell_bridge.lua
```
Command in-game: `/dshell` untuk toggle visibility.

---

## Touch Input

ImGui touch input perlu hook tambahan ke Android input system.  
Saat ini `devshell_inject_touch(x, y, down)` tersedia sebagai export  
yang bisa dipanggil dari Lua bridge atau hook input tersendiri.

---

## Struktur

```
libdevshell/
├── mod/
│   └── main.cpp              # Hook eglSwapBuffers + ImGui render + semua widget
├── include/
│   ├── imgui/
│   │   ├── imconfig.h        # Config ImGui (ES2, ARM32)
│   │   └── [imgui source]    # Download via CI atau manual
│   └── mod/
│       └── amlmod.h          # AML minimal header
├── jni/
│   ├── Android.mk
│   └── Application.mk
├── .github/workflows/
│   └── build.yml
└── devshell_bridge.lua       # Lua bridge opsional
```

---

Author: brruham-arch
