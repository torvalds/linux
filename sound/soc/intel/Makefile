# Core support
obj-$(CONFIG_SND_SOC_INTEL_SST) += common/

snd-soc-sst-mfld-platform-objs := sst-mfld-platform-pcm.o \
	sst-mfld-platform-compress.o sst-atom-controls.o
snd-soc-mfld-machine-objs := mfld_machine.o

obj-$(CONFIG_SND_SST_MFLD_PLATFORM) += snd-soc-sst-mfld-platform.o
obj-$(CONFIG_SND_MFLD_MACHINE) += snd-soc-mfld-machine.o

# Platform Support
obj-$(CONFIG_SND_SOC_INTEL_HASWELL) += haswell/
snd-soc-sst-baytrail-pcm-objs := \
	sst-baytrail-ipc.o sst-baytrail-pcm.o sst-baytrail-dsp.o

obj-$(CONFIG_SND_SOC_INTEL_BAYTRAIL) += snd-soc-sst-baytrail-pcm.o

# Machine support
obj-$(CONFIG_SND_SOC_INTEL_SST) += boards/

# DSP driver
obj-$(CONFIG_SND_SST_IPC) += sst/
