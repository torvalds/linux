# SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
# This file is provided under a dual BSD/GPLv2 license. When using or
# redistributing this file, you may do so under either license.
#
# Copyright(c) 2021, 2023 Advanced Micro Devices, Inc. All rights reserved.

snd-sof-amd-acp-y := acp.o acp-loader.o acp-ipc.o acp-pcm.o acp-stream.o acp-trace.o acp-common.o
snd-sof-amd-acp-$(CONFIG_SND_SOC_SOF_ACP_PROBES) += acp-probes.o
snd-sof-amd-renoir-y := pci-rn.o renoir.o
snd-sof-amd-rembrandt-y := pci-rmb.o rembrandt.o
snd-sof-amd-vangogh-y := pci-vangogh.o vangogh.o
snd-sof-amd-acp63-y := pci-acp63.o acp63.o

obj-$(CONFIG_SND_SOC_SOF_AMD_COMMON) += snd-sof-amd-acp.o
obj-$(CONFIG_SND_SOC_SOF_AMD_RENOIR) += snd-sof-amd-renoir.o
obj-$(CONFIG_SND_SOC_SOF_AMD_REMBRANDT) += snd-sof-amd-rembrandt.o
obj-$(CONFIG_SND_SOC_SOF_AMD_VANGOGH) += snd-sof-amd-vangogh.o
obj-$(CONFIG_SND_SOC_SOF_AMD_ACP63) += snd-sof-amd-acp63.o
