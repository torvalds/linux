# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

snd-sof-y := core.o ops.o loader.o ipc.o pcm.o pm.o debug.o topology.o\
		control.o trace.o iomem-utils.o sof-audio.o stream-ipc.o\
		fw-file-profile.o

# IPC implementations
ifneq ($(CONFIG_SND_SOC_SOF_IPC3),)
snd-sof-y +=	ipc3.o ipc3-loader.o ipc3-topology.o ipc3-control.o ipc3-pcm.o\
		ipc3-dtrace.o
endif
ifneq ($(CONFIG_SND_SOC_SOF_IPC4),)
snd-sof-y += ipc4.o ipc4-loader.o ipc4-topology.o ipc4-control.o ipc4-pcm.o\
		ipc4-mtrace.o ipc4-telemetry.o
endif

# SOF client support
ifneq ($(CONFIG_SND_SOC_SOF_CLIENT),)
snd-sof-y += sof-client.o
endif

snd-sof-$(CONFIG_SND_SOC_SOF_COMPRESS) += compress.o

snd-sof-pci-y := sof-pci-dev.o
snd-sof-acpi-y := sof-acpi-dev.o
snd-sof-of-y := sof-of-dev.o

snd-sof-ipc-flood-test-y := sof-client-ipc-flood-test.o
snd-sof-ipc-msg-injector-y := sof-client-ipc-msg-injector.o
snd-sof-ipc-kernel-injector-y := sof-client-ipc-kernel-injector.o
snd-sof-probes-y := sof-client-probes.o
ifneq ($(CONFIG_SND_SOC_SOF_IPC3),)
snd-sof-probes-y += sof-client-probes-ipc3.o
endif
ifneq ($(CONFIG_SND_SOC_SOF_IPC4),)
snd-sof-probes-y += sof-client-probes-ipc4.o
endif

snd-sof-nocodec-y := nocodec.o

snd-sof-utils-y := sof-utils.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof.o
obj-$(CONFIG_SND_SOC_SOF_NOCODEC) += snd-sof-nocodec.o

obj-$(CONFIG_SND_SOC_SOF) += snd-sof-utils.o

obj-$(CONFIG_SND_SOC_SOF_ACPI_DEV) += snd-sof-acpi.o
obj-$(CONFIG_SND_SOC_SOF_OF_DEV) += snd-sof-of.o
obj-$(CONFIG_SND_SOC_SOF_PCI_DEV) += snd-sof-pci.o

obj-$(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST) += snd-sof-ipc-flood-test.o
obj-$(CONFIG_SND_SOC_SOF_DEBUG_IPC_MSG_INJECTOR) += snd-sof-ipc-msg-injector.o
obj-$(CONFIG_SND_SOC_SOF_DEBUG_IPC_KERNEL_INJECTOR) += snd-sof-ipc-kernel-injector.o
obj-$(CONFIG_SND_SOC_SOF_DEBUG_PROBES) += snd-sof-probes.o

obj-$(CONFIG_SND_SOC_SOF_INTEL_TOPLEVEL) += intel/
obj-$(CONFIG_SND_SOC_SOF_IMX_TOPLEVEL) += imx/
obj-$(CONFIG_SND_SOC_SOF_AMD_TOPLEVEL) += amd/
obj-$(CONFIG_SND_SOC_SOF_XTENSA) += xtensa/
obj-$(CONFIG_SND_SOC_SOF_MTK_TOPLEVEL) += mediatek/
