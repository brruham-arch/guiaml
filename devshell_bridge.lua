-- devshell_bridge.lua
-- Bridge opsional: toggle DevShell overlay dari chat command SA-MP
-- Taruh di: /Android/media/com.sampmobilerp.game/monetloader/
--
-- CATATAN:
-- .so harus sudah dimuat AML lebih dulu.
-- Script ini hanya bridge untuk toggle, touch inject sudah otomatis
-- dari imgui IO jika kamu menambahkan hook input nanti.

script_name('DevShellBridge')
script_author('brruham-arch')
script_version('1.0')
script_properties('work-in-pause')

local ffi = require 'ffi'

-- ── FFI declaration ────────────────────────────────────────────────────────
ffi.cdef[[
    void devshell_toggle(void);
    int  devshell_is_visible(void);
    void devshell_inject_touch(float x, float y, int down);
]]

-- ── Load library ──────────────────────────────────────────────────────────
local lib = nil
local function try_load()
    local paths = {
        '/data/data/com.sampmobilerp.game/lib/libdevshell.so',
        '/data/app/~~*/com.sampmobilerp.game-*/lib/arm/libdevshell.so',
    }
    -- AML sudah load, coba dlopen via ffi
    local ok, err = pcall(function()
        lib = ffi.load('devshell')
    end)
    if not ok then
        sampAddChatMessage('[DevShell] Bridge: lib tidak ditemukan - ' .. tostring(err), 0xFF4444)
        lib = nil
    else
        sampAddChatMessage('[DevShell] Bridge: terhubung ke libdevshell.so', 0x44FF88)
    end
end

-- ── Main ──────────────────────────────────────────────────────────────────
function main()
    while not isSampAvailable() do wait(100) end

    wait(1000)  -- tunggu AML selesai load semua .so
    try_load()

    -- Toggle overlay
    sampRegisterChatCommand('dshell', function()
        if lib then
            lib.devshell_toggle()
            local vis = lib.devshell_is_visible()
            sampAddChatMessage('[DevShell] ' .. (vis == 1 and 'Visible' or 'Hidden'), 0x44FF88)
        else
            sampAddChatMessage('[DevShell] .so tidak tersambung', 0xFF4444)
        end
    end)

    -- Touch inject manual dari Lua (opsional, untuk test)
    -- Penggunaan: /dtouch 540 960 1  (x y down)
    sampRegisterChatCommand('dtouch', function(args)
        if not lib then return end
        local x, y, d = args:match('(%S+)%s+(%S+)%s+(%S+)')
        if x then
            lib.devshell_inject_touch(tonumber(x), tonumber(y), tonumber(d) or 0)
        end
    end)

    while true do wait(1000) end
end
