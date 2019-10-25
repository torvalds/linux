# SPDX-License-Identifier: GPL-2.0
snd-soc-sst-dsp-objs := sst-dsp.o
snd-soc-sst-acpi-objs := sst-acpi.o
snd-soc-sst-ipc-objs := sst-ipc.o
snd-soc-sst-firmware-objs := sst-firmware.o
snd-soc-acpi-intel-match-objs := soc-acpi-intel-byt-match.o soc-acpi-intel-cht-match.o \
	soc-acpi-intel-hsw-bdw-match.o \
	soc-acpi-intel-skl-match.o soc-acpi-intel-kbl-match.o \
	soc-acpi-intel-bxt-match.o soc-acpi-intel-glk-match.o \
	soc-acpi-intel-cnl-match.o soc-acpi-intel-icl-match.o \
	soc-acpi-intel-tgl-match.o soc-acpi-intel-ehl-match.o \
	soc-acpi-intel-hda-match.o

obj-$(CONFIG_SND_SOC_INTEL_SST) += snd-soc-sst-dsp.o snd-soc-sst-ipc.o
obj-$(CONFIG_SND_SOC_INTEL_SST_ACPI) += snd-soc-sst-acpi.o
obj-$(CONFIG_SND_SOC_INTEL_SST_FIRMWARE) += snd-soc-sst-firmware.o
obj-$(CONFIG_SND_SOC_ACPI_INTEL_MATCH) += snd-soc-acpi-intel-match.o
