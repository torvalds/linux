snd-soc-sst-dsp-objs := sst-dsp.o
snd-soc-sst-acpi-objs := sst-acpi.o
snd-soc-sst-match-objs := sst-match-acpi.o
snd-soc-sst-ipc-objs := sst-ipc.o

snd-soc-sst-dsp-$(CONFIG_DW_DMAC_CORE) += sst-firmware.o

obj-$(CONFIG_SND_SOC_INTEL_SST) += snd-soc-sst-dsp.o snd-soc-sst-ipc.o
obj-$(CONFIG_SND_SOC_INTEL_SST_ACPI) += snd-soc-sst-acpi.o
obj-$(CONFIG_SND_SOC_INTEL_SST_MATCH) += snd-soc-sst-match.o
