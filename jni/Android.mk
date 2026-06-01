LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := guiaml

LOCAL_SRC_FILES := \
    main.cpp \
    ../include/imgui/imgui.cpp \
    ../include/imgui/imgui_draw.cpp \
    ../include/imgui/imgui_tables.cpp \
    ../include/imgui/imgui_widgets.cpp \
    ../include/imgui/imgui_impl_opengl3.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../include

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -O2 \
    -fvisibility=hidden \
    -DIMGUI_DISABLE_DEMO_WINDOWS \
    -DIMGUI_DISABLE_DEBUG_TOOLS \
    -DIMGUI_IMPL_OPENGL_ES2

LOCAL_LDLIBS := -llog -ldl -landroid -lEGL -lGLESv2

include $(BUILD_SHARED_LIBRARY)
