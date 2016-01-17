snd-soc-sst-dsp-objs := sst-dsp.o
ifneq ($(CONFIG_SND_SST_IPC_ACPI),)
snd-soc-sst-acpi-objs := sst-match-acpi.o
else
snd-soc-sst-acpi-objs := sst-acpi.o sst-match-acpi.o
endif

snd-soc-sst-ipc-objs := sst-ipc.o

snd-soc-sst-dsp-$(CONFIG_DW_DMAC_CORE) += sst-firmware.o

obj-$(CONFIG_SND_SOC_INTEL_SST) += snd-soc-sst-dsp.o snd-soc-sst-ipc.o
obj-$(CONFIG_SND_SOC_INTEL_SST_ACPI) += snd-soc-sst-acpi.o
