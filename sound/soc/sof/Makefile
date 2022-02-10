# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-objs := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o trace.o iomem-utils.o sof-audio.o stream-ipc.o
snd-sof-$(CONFIG_SND_SOC_SOF_CLIENT) += sof-client.o

snd-sof-$(CONFIG_SND_SOC_SOF_DEBUG_PROBES) += sof-probes.o
snd-sof-$(CONFIG_SND_SOC_SOF_COMPRESS) += compress.o

snd-sof-pci-objs := sof-pci-dev.o
snd-sof-acpi-objs := sof-acpi-dev.o
snd-sof-of-objs := sof-of-dev.o

snd-sof-nocodec-objs := nocodec.o

snd-sof-utils-objs := sof-utils.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_NOCODEC) += snd-sof-nocodec.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof-utils.o

obj-$(CONFIG_SND_SOC_SOF_ACPI_DEV) += snd-sof-acpi.o
obj-$(CONFIG_SND_SOC_SOF_OF_DEV) += snd-sof-of.o
obj-$(CONFIG_SND_SOC_SOF_PCI_DEV) += snd-sof-pci.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_TOPLEVEL) += intel/
obj-$(CONFIG_SND_SOC_SOF_IMX_TOPLEVEL) += imx/
obj-$(CONFIG_SND_SOC_SOF_AMD_TOPLEVEL) += amd/
obj-$(CONFIG_SND_SOC_SOF_XTENSA) += xtensa/
obj-$(CONFIG_SND_SOC_SOF_MTK_TOPLEVEL) += mediatek/
