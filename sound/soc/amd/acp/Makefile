# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
# This file is provided under a dual BSD/GPLv2 license. When using or
# redistributing this file, you may do so under either license.
#
# Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.

#common acp driver
snd-acp-pcm-y     := acp-platform.o
snd-acp-i2s-y     := acp-i2s.o
snd-acp-pdm-y     := acp-pdm.o
snd-acp-legacy-common-y   := acp-legacy-common.o
snd-acp-pci-y     := acp-pci.o
snd-amd-sdw-acpi-y := amd-sdw-acpi.o

#platform specific driver
snd-acp-renoir-y     := acp-renoir.o
snd-acp-rembrandt-y  := acp-rembrandt.o
snd-acp63-y := acp63.o
snd-acp70-y := acp70.o

#machine specific driver
snd-acp-mach-y     := acp-mach-common.o
snd-acp-legacy-mach-y     := acp-legacy-mach.o acp3x-es83xx/acp3x-es83xx.o
snd-acp-sof-mach-y     := acp-sof-mach.o

obj-$(CONFIG_SND_SOC_AMD_ACP_PCM) += snd-acp-pcm.o
obj-$(CONFIG_SND_SOC_AMD_ACP_I2S) += snd-acp-i2s.o
obj-$(CONFIG_SND_SOC_AMD_ACP_PDM) += snd-acp-pdm.o
obj-$(CONFIG_SND_SOC_AMD_ACP_LEGACY_COMMON) += snd-acp-legacy-common.o
obj-$(CONFIG_SND_SOC_AMD_ACP_PCI) += snd-acp-pci.o

obj-$(CONFIG_SND_AMD_ASOC_RENOIR) += snd-acp-renoir.o
obj-$(CONFIG_SND_AMD_ASOC_REMBRANDT) += snd-acp-rembrandt.o
obj-$(CONFIG_SND_AMD_ASOC_ACP63) += snd-acp63.o
obj-$(CONFIG_SND_AMD_ASOC_ACP70) += snd-acp70.o

obj-$(CONFIG_SND_AMD_SOUNDWIRE_ACPI) += snd-amd-sdw-acpi.o
obj-$(CONFIG_SND_SOC_AMD_MACH_COMMON) += snd-acp-mach.o
obj-$(CONFIG_SND_SOC_AMD_LEGACY_MACH) += snd-acp-legacy-mach.o
obj-$(CONFIG_SND_SOC_AMD_SOF_MACH) += snd-acp-sof-mach.o
