LOCAL_PATH := $(call my-dir)
COMMON := $(dir $(LOCAL_PATH))common

include $(CLEAR_VARS)
LOCAL_SRC_FILES := gpio-keys.kcm
include $(BUILD_KEY_CHAR_MAP)

SUBDIRS := \
	$(LOCAL_PATH)/libsensors/Android.mk \
	$(COMMON)/AndroidBoardCommon.mk

include $(SUBDIRS)
