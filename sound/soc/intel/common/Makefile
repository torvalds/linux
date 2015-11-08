snd-soc-sst-dsp-objs := sst-dsp.o
snd-soc-sst-acpi-objs := sst-acpi.o
snd-soc-sst-ipc-objs := sst-ipc.o

ifneq ($(CONFIG_DW_DMAC_CORE),)
snd-soc-sst-dsp-objs += sst-firmware.o
endif

obj-$(CONFIG_SND_SOC_INTEL_SST) += snd-soc-sst-dsp.o snd-soc-sst-ipc.o
obj-$(CONFIG_SND_SOC_INTEL_SST_ACPI) += snd-soc-sst-acpi.o

