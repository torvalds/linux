# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

ccflags-y += -DDEBUG

snd-sof-objs := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o trace.o compressed.o utils.o
snd-sof-spi-objs := hw-spi.o

snd-sof-pci-objs := sof-pci-dev.o
snd-sof-acpi-objs := sof-acpi-dev.o
snd-sof-nocodec-objs := nocodec.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_NOCODEC) += snd-sof-nocodec.o

obj-$(CONFIG_SND_SOC_SOF_ACPI) += sof-acpi-dev.o
obj-$(CONFIG_SND_SOC_SOF_PCI) += sof-pci-dev.o
obj-$(CONFIG_SND_SOC_SOF_SPI) += sof-spi-dev.o

obj-$(CONFIG_SND_SOC_SOF_SPIDSP) += snd-sof-spi.o

obj-$(CONFIG_SND_SOC_SOF_INTEL)	+= intel/
obj-$(CONFIG_SND_SOC_SOF_XTENSA) += xtensa/
