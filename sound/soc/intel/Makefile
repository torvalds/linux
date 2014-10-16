# Core support
snd-soc-sst-dsp-objs := sst-dsp.o sst-firmware.o
snd-soc-sst-acpi-objs := sst-acpi.o

snd-soc-sst-mfld-platform-objs := sst-mfld-platform-pcm.o \
	sst-mfld-platform-compress.o sst-atom-controls.o
snd-soc-mfld-machine-objs := mfld_machine.o

obj-$(CONFIG_SND_SST_MFLD_PLATFORM) += snd-soc-sst-mfld-platform.o
obj-$(CONFIG_SND_MFLD_MACHINE) += snd-soc-mfld-machine.o

obj-$(CONFIG_SND_SOC_INTEL_SST) += snd-soc-sst-dsp.o
obj-$(CONFIG_SND_SOC_INTEL_SST_ACPI) += snd-soc-sst-acpi.o

# Platform Support
snd-soc-sst-haswell-pcm-objs := \
	sst-haswell-ipc.o sst-haswell-pcm.o sst-haswell-dsp.o
snd-soc-sst-baytrail-pcm-objs := \
	sst-baytrail-ipc.o sst-baytrail-pcm.o sst-baytrail-dsp.o

obj-$(CONFIG_SND_SOC_INTEL_HASWELL) += snd-soc-sst-haswell-pcm.o
obj-$(CONFIG_SND_SOC_INTEL_BAYTRAIL) += snd-soc-sst-baytrail-pcm.o

# Machine support
snd-soc-sst-haswell-objs := haswell.o
snd-soc-sst-byt-rt5640-mach-objs := byt-rt5640.o
snd-soc-sst-byt-max98090-mach-objs := byt-max98090.o
snd-soc-sst-broadwell-objs := broadwell.o

obj-$(CONFIG_SND_SOC_INTEL_HASWELL_MACH) += snd-soc-sst-haswell.o
obj-$(CONFIG_SND_SOC_INTEL_BYT_RT5640_MACH) += snd-soc-sst-byt-rt5640-mach.o
obj-$(CONFIG_SND_SOC_INTEL_BYT_MAX98090_MACH) += snd-soc-sst-byt-max98090-mach.o
obj-$(CONFIG_SND_SOC_INTEL_BROADWELL_MACH) += snd-soc-sst-broadwell.o

# DSP driver
obj-$(CONFIG_SND_SST_IPC) += sst/
