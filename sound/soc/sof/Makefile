# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-objs := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o trace.o iomem-utils.o sof-audio.o stream-ipc.o

# IPC implementations
ifneq ($(CONFIG_SND_SOC_SOF_IPC3),)
snd-sof-objs +=	ipc3.o ipc3-loader.o ipc3-topology.o ipc3-control.o ipc3-pcm.o\
		ipc3-dtrace.o
endif
ifneq ($(CONFIG_SND_SOC_SOF_INTEL_IPC4),)
snd-sof-objs += ipc4.o ipc4-loader.o ipc4-topology.o ipc4-control.o ipc4-pcm.o\
		ipc4-mtrace.o
endif

# SOF client support
ifneq ($(CONFIG_SND_SOC_SOF_CLIENT),)
snd-sof-objs += sof-client.o
endif

snd-sof-$(CONFIG_SND_SOC_SOF_COMPRESS) += compress.o

snd-sof-pci-objs := sof-pci-dev.o
snd-sof-acpi-objs := sof-acpi-dev.o
snd-sof-of-objs := sof-of-dev.o

snd-sof-ipc-flood-test-objs := sof-client-ipc-flood-test.o
snd-sof-ipc-msg-injector-objs := sof-client-ipc-msg-injector.o
snd-sof-probes-objs := sof-client-probes.o

snd-sof-nocodec-objs := nocodec.o

snd-sof-utils-objs := sof-utils.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_NOCODEC) += snd-sof-nocodec.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof-utils.o

obj-$(CONFIG_SND_SOC_SOF_ACPI_DEV) += snd-sof-acpi.o
obj-$(CONFIG_SND_SOC_SOF_OF_DEV) += snd-sof-of.o
obj-$(CONFIG_SND_SOC_SOF_PCI_DEV) += snd-sof-pci.o

obj-$(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST) += snd-sof-ipc-flood-test.o
obj-$(CONFIG_SND_SOC_SOF_DEBUG_IPC_MSG_INJECTOR) += snd-sof-ipc-msg-injector.o
obj-$(CONFIG_SND_SOC_SOF_DEBUG_PROBES) += snd-sof-probes.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_TOPLEVEL) += intel/
obj-$(CONFIG_SND_SOC_SOF_IMX_TOPLEVEL) += imx/
obj-$(CONFIG_SND_SOC_SOF_AMD_TOPLEVEL) += amd/
obj-$(CONFIG_SND_SOC_SOF_XTENSA) += xtensa/
obj-$(CONFIG_SND_SOC_SOF_MTK_TOPLEVEL) += mediatek/
