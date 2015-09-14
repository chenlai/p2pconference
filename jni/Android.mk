#
# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

#ENABLE_FEATURES += -DSEND_RESOLUTION_720P
#ENABLE_FEATURES += -DRECEIVE_RESOLUTION_720P

ENABLE_FEATURES += -DUSE_SPEAKER
ENABLE_FEATURES += -DAUDIO_ENABLED
ENABLE_FEATURES += -DENABLE_DECODING
ENABLE_FEATURES += -DENABLE_ENCODING
ENABLE_FEATURES += -DENABLE_DIRECT_DECODE
#ENABLE_FEATURES += -DUSE_AWESOME_PLAYER
#ENABLE_FEATURES += -DAWESOME_LOCAL_LOOPBACK

# Enable this macro to play the local test file, Its expected that test video file named testvideo-320x240.264
# file has to be copied into data/data/com.p2p.app/ target folder
#ENABLE_FEATURES += -DENABLE_TEST_MODE

#Enable this to dump send/recv RTP data into file
#ENABLE_FEATURES += -DENABLE_TEST_DUMP_RTP

# Enable this to enable frame profiling using counters and timers
#ENABLE_FEATURES += -DENABLE_PROFILING

#Enable this for debugging
#LOCAL_STRIP_MODULE := false

CSS_IMB_PATH := $(TOP)/hardware/dspg/dmw96/css/imb

LOCAL_SRC_FILES := Transport.cpp \
		NativeCodeCaller.cpp \
		CameraController.cpp \
		VideoEncoderController.cpp \
		MediaController.cpp \
		MediaPlayerCtrl.cpp \
		VideoRecorderCtrl.cpp \
		AudioRecorderCtrl.cpp \
		ARTPStreamer.cpp \
		player/DSPGAudioPlayer.cpp \
		player/DSPGAudioSource.cpp \
		player/DSPGMediaPlayer.cpp \
		player/DSPGAudioOutput.cpp \
		player/utils/DSPGTimedEventQueue.cpp

LOCAL_C_INCLUDES := $(TOP)/dalvik/libnativehelper/include \
		    $(TOP)/frameworks/base/include \
		    $(TOP)/frameworks/base/include/media/stagefright/openmax \
		    $(TOP)/frameworks/base/media/libstagefright/include \
		    $(TOP)/frameworks/base/media/libstagefright/rtsp \
		    $(TOP)/hardware/libhardware/include \
		    $(TOP)/system/core/include \
   		    $(TOP)/packages/apps/dspg/P2PVideoPhoneApp/jni/player/rtsp \
   		    $(TOP)/packages/apps/dspg/P2PVideoPhoneApp/jni/player \
   		    $(TOP)/packages/apps/dspg/P2PVideoPhoneApp/jni/player/utils \
		    $(CSS_IMB_PATH)/include \
		    $(CSS_IMB_PATH)/include \
		    $(CSS_IMB_PATH)/libch  \
		    $(CSS_IMB_PATH)/librtp \
		    $(CSS_IMB_PATH)/libss7  \
		    $(CSS_IMB_PATH)/msgswitch \
		    $(CSS_IMB_PATH)/srtp/include \
		    $(CSS_IMB_PATH)/srtp/crypto/include \
		    $(TOP)/kernel/arch/arm/mach-dmw/css/

LOCAL_STATIC_LIBRARIES := \
        libp2p_rtsp \
        libch \
        librtp \
        libss7 \
        libsrtp \
        libdspapi 
		    
LOCAL_CFLAGS := -DSKYPEKIT34 \
		-DMODIFIED_METADATA \
		-DGINGERBREAD \
		$(ENABLE_FEATURES) \
		-DLOG_MASK=0x0f -DENABLE_DEBUG \
		-DSKYPEKIT_ROOT_PATH=\"/data/data/com.skype.ref/files\" \
		-Dlinux

LOCAL_SHARED_LIBRARIES := libandroid_runtime \
			  libstagefright \
			  libutils \
			  libnativehelper \
			  libcamera_client \
			  libsurfaceflinger_client \
			  libmedia \
			  libbinder \
              libstagefright_foundation \
              libcutils \
              libsurfaceflinger_client \
              libstagefright_color_conversion \
              libstagefright_omx
			  
ifneq ($(TARGET_SIMULATOR),true)
LOCAL_SHARED_LIBRARIES += libdl
endif

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libP2PConference

include $(BUILD_SHARED_LIBRARY)

include $(LOCAL_PATH)/player/rtsp/Android.mk

##################################################

