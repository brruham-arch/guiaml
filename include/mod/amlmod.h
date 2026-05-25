#pragma once
// AML Mod Header - minimal required subset
// Source: AndroidModLoader SDK

#ifndef MYMOD
#define MYMOD(guid, name, ver, author) \
    extern "C" { \
        __attribute__((visibility("default"))) \
        void* __GetModInfo() { \
            static const char* info = #name "|" #ver "|AML Mod|" #author; \
            return (void*)info; \
        } \
    } \
    struct _AMLAutoInit { \
        _AMLAutoInit(); \
    }; \
    static _AMLAutoInit _amlAutoInit; \
    _AMLAutoInit::_AMLAutoInit()
#endif

#ifndef ON_MOD_PRELOAD
#define ON_MOD_PRELOAD() \
    extern "C" __attribute__((visibility("default"))) void OnModPreLoad()
#endif

#ifndef ON_MOD_LOAD
#define ON_MOD_LOAD() \
    extern "C" __attribute__((visibility("default"))) void OnModLoad()
#endif
