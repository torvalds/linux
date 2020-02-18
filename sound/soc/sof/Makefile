# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

snd-sof-objs := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o trace.o utils.o sof-audio.o
snd-sof-$(CONFIG_SND_SOC_SOF_DEBUG_PROBES) += probe.o

snd-sof-pci-objs := sof-pci-dev.o
snd-sof-acpi-objs := sof-acpi-dev.o
snd-sof-of-objs := sof-of-dev.o

snd-sof-nocodec-objs := nocodec.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_NOCODEC) += snd-sof-nocodec.o


obj-$(CONFIG_SND_SOC_SOF_ACPI) += snd-sof-acpi.o
obj-$(CONFIG_SND_SOC_SOF_OF) += snd-sof-of.o
obj-$(CONFIG_SND_SOC_SOF_PCI) += snd-sof-pci.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_TOPLEVEL) += intel/
obj-$(CONFIG_SND_SOC_SOF_IMX_TOPLEVEL) += imx/
obj-$(CONFIG_SND_SOC_SOF_XTENSA) += xtensa/
