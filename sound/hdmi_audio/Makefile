DRIVER_NAME := hdmi_audio

ccflags-y += -Idrivers/gpu/drm/i915

$(DRIVER_NAME)-objs += \
	intel_mid_hdmi_audio.o \
	intel_mid_hdmi_audio_if.o

obj-m += $(DRIVER_NAME).o
