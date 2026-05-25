LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := guiaml

LOCAL_SRC_FILES := \
    ../mod/main.cpp \
    ../include/imgui/imgui.cpp \
    ../include/imgui/imgui_draw.cpp \
    ../include/imgui/imgui_tables.cpp \
    ../include/imgui/imgui_widgets.cpp \
    ../include/imgui/imgui_impl_opengl3.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../include/imgui \
    $(LOCAL_PATH)/../include/mod

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -O2 \
    -fPIC \
    -fvisibility=hidden \
    -ffunction-sections \
    -fdata-sections \
    -mthumb \
    -DIMGUI_IMPL_OPENGL_ES2 \
    -DIMGUI_DISABLE_DEMO_WINDOWS \
    -DIMGUI_USER_CONFIG="\"imconfig.h\""

LOCAL_LDLIBS := \ -landroid
    -llog \
    -lm \
    -ldl \
    -lEGL \
    -lGLESv2

LOCAL_LDFLAGS := \
    -static-libstdc++ \
    -Wl,--gc-sections

include $(BUILD_SHARED_LIBRARY)
