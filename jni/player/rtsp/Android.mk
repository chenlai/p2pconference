LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=       \
        DSPGAAMRAssembler.cpp           \
        DSPGAAVCAssembler.cpp           \
        DSPGAH263Assembler.cpp          \
        DSPGAMPEG4AudioAssembler.cpp    \
        DSPGAMPEG4ElementaryAssembler.cpp \
        DSPGAPacketSource.cpp           \
        DSPGARTPAssembler.cpp           \
        DSPGARTPConnection.cpp          \
        DSPGARTPSession.cpp             \
        DSPGARTPSource.cpp              \
        DSPGARTPWriter.cpp              \
        DSPGARTSPConnection.cpp         \
        DSPGARTSPController.cpp         \
        DSPGASessionDescription.cpp     \
        DSPGUDPPusher.cpp               \

LOCAL_C_INCLUDES:= \
	$(JNI_H_INCLUDE) \
	$(TOP)/frameworks/base/include/media/stagefright/openmax \
        $(TOP)/frameworks/base/media/libstagefright/include \
        $(TOP)/external/openssl/include

# Enable this to enable frame profiling using counters and timers
#LOCAL_CFLAGS += -DENABLE_PROFILING

LOCAL_MODULE:= libp2p_rtsp

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)

################################################################################


