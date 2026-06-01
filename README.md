# guiaml v1.1 — ImGui Overlay (Fixed Touch)

AML native mod: GUI ImGui di GTA SA Android via `eglSwapBuffers` hook.  
**v1.1** — memperbaiki semua bug touch dari versi sebelumnya.

---

## Bug yang Diperbaiki dari Versi Lama

| # | Bug Lama | Fix v1.1 |
|---|----------|----------|
| 1 | Touch blocking pakai manual `touch_in_gui()` rect check | Ganti ke `io.WantCaptureMouse` — cara yang benar |
| 2 | `MousePos`/`MouseDown` di-set **setelah** `NewFrame()` | Dipindah ke `feed_touch_to_imgui()` **sebelum** `NewFrame()` |
| 3 | `action==2` (MOVE) di-treat sebagai button DOWN | MOVE hanya update posisi, DOWN/UP yang ubah state button |
| 4 | GL state tidak di-save/restore → bisa corrupt render game | Save semua state penting, restore setelah ImGui render |
| 5 | `g_touch.down` tidak pernah false saat jari lepas | Action UP (1, 6) sekarang send `AddMouseButtonEvent(0, false)` |
| 6 | `WantCaptureMouse` tidak pernah dipakai | Sekarang jadi penentu utama apakah touch di-consume |

### Penjelasan Fix #1 (paling kritis)

```cpp
// ❌ SALAH (versi lama) — race condition + tidak reliable
static bool touch_in_gui(float x, float y) {
    WinRect r = g_win_rect;  // rect belum valid di frame pertama!
    return (x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h);
}

// ✅ BENAR (v1.1) — ImGui track sendiri hover/active window
if (g_visible && ImGui::GetIO().WantCaptureMouse) {
    return; // consume, jangan forward ke game
}
```

### Penjelasan Fix #2 (urutan feed touch)

```cpp
// ❌ SALAH (versi lama) — ImGui tidak proses input ini dengan benar
ImGui_ImplOpenGL3_NewFrame();   // ← NewFrame dulu
ImGui::NewFrame();
io.MousePos = ...;              // ← baru set, tapi sudah telat
io.MouseDown[0] = ...;

// ✅ BENAR (v1.1)
feed_touch_to_imgui();          // ← feed dulu pakai AddMousePosEvent/AddMouseButtonEvent
ImGui_ImplOpenGL3_NewFrame();
ImGui::NewFrame();              // ← baru NewFrame, sudah punya input yang benar
```

---

## Setup

### 1. Struktur repo

```
guiaml/
├── jni/
│   ├── main.cpp
│   ├── Android.mk
│   └── Application.mk
├── include/
│   └── imgui/
│       ├── imgui.h
│       ├── imgui.cpp
│       ├── imgui_draw.cpp
│       ├── imgui_tables.cpp
│       ├── imgui_widgets.cpp
│       ├── imgui_impl_opengl3.h
│       ├── imgui_impl_opengl3.cpp
│       └── imgui_internal.h
└── .github/workflows/build.yml
```

### 2. Tambahkan ImGui source

Clone ImGui ke `include/imgui/`:
```bash
# Di Termux atau Actions:
git clone --depth=1 https://github.com/ocornut/imgui.git include/imgui
# Pastikan pakai versi yang support ImGuiIO::AddMousePosEvent (>= 1.87)
```

Atau tambahkan sebagai git submodule:
```bash
git submodule add https://github.com/ocornut/imgui.git include/imgui
```

### 3. Sesuaikan offset AND_TouchEvent

Buka `jni/main.cpp`, cari baris ini dan ganti offset sesuai versi `libGTASA.so` kamu:

```cpp
void* addr_touch = (void*)(gtasa_base + 0x2697C0);  // ← ganti ini
```

Cara cari offset:
```bash
# Di Termux, dari libGTASA.so yang ada di device:
nm -D /data/app/com.rockstargames.gtasa*/lib/arm/libGTASA.so | grep -i touch
# Kalau stripped, cek dari mod referensi atau IDA/Ghidra
```

### 4. Build via GitHub Actions

Push ke repo → Actions otomatis build → download `libguiaml.so` dari Artifacts.

### 5. Pasang ke game

Letakkan `libguiaml.so` di folder AML:
```
/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/AML/
```

---

## Cara Pakai

- GUI muncul otomatis saat game load
- Tap **[X]** di pojok window untuk minimize jadi tombol kecil `GUIAML`
- Tap tombol `GUIAML` untuk buka kembali
- Semua touch di luar area window GUI diteruskan normal ke game

---

## Log Debug

Cek log di:
```
/storage/emulated/0/guiaml_log.txt
```

Baris penting yang harus ada jika berhasil:
```
[GUIAML] eglSwapBuffers hooked OK
[GUIAML] AND_TouchEvent hooked OK
[GUIAML] ImGui init OK 1080x1920 scale=2.20
```
