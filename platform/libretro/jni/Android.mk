LOCAL_PATH := $(call my-dir)

DEBUG                    := 0
FLAGS                    :=

CORE_DIR := $(LOCAL_PATH)/../../..

include $(LOCAL_PATH)/../Makefile.common

COREFLAGS := $(INCFLAGS) $(FLAGS)
COREFLAGS += -D__LIBRETRO__

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES_C)
LOCAL_CFLAGS       := $(COREFLAGS)
LOCAL_CXXFLAGS     := $(COREFLAGS)
LOCAL_LDFLAGS      := -Wl,-version-script=$(CORE_DIR)/platform/libretro/link.T
LOCAL_CPP_FEATURES := exceptions
include $(BUILD_SHARED_LIBRARY)
