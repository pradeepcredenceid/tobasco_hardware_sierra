#
# This source code is "Not a Contribution" under Apache license
#
# Sierra Wireless Firmware Download application
#
# Based on reference-ril by The Android Open Source Project
# Modified by Sierra Wireless, Inc.
#
# Copyright (C) 2015 Sierra Wireless, Inc.
# Copyright 2006 The Android Open Source Project
#
LOCAL_PATH:= $(call my-dir)

# For executable
# =======================
include $(CLEAR_VARS)

LOCAL_CFLAGS:=
LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

# Check - see if this is a 64 build
ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), x86_64 arm64))
    LOCAL_CFLAGS += -DCPU64
endif

ifeq ($(TARGET_PRODUCT),hikey)
    LOCAL_C_INCLUDES :=
endif
   
# Source files for this build
LOCAL_SRC_FILES:=   swi_firmware_download.c \
                    swi_at_channel.c \
                    swi_at_tok.c


LOCAL_SHARED_LIBRARIES := \
	libcutils libswisdkapi
	

# Disable prelink
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_TAGS := optional

# Build test executable
LOCAL_MODULE:= fwdnld8092

include $(BUILD_EXECUTABLE)

