LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libtermux-native
LOCAL_SRC_FILES := termux-native.c \
	android-dl.cpp \
	patchelf.cpp \
	common.cpp
LOCAL_LDLIBS    := -ldl -llog
LOCAL_CXXFLAGS  := -fexceptions
include $(BUILD_SHARED_LIBRARY)
