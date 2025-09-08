# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../../common

snd-hda-codec-hdmi-y :=		hdmi.o eld.o
snd-hda-codec-simplehdmi-y :=	simplehdmi.o
snd-hda-codec-intelhdmi-y :=	intelhdmi.o
snd-hda-codec-atihdmi-y :=	atihdmi.o
snd-hda-codec-nvhdmi-y :=	nvhdmi.o
snd-hda-codec-nvhdmi-mcp-y :=	nvhdmi-mcp.o
snd-hda-codec-tegrahdmi-y :=	tegrahdmi.o

obj-$(CONFIG_SND_HDA_CODEC_HDMI_GENERIC) += snd-hda-codec-hdmi.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_SIMPLE) += snd-hda-codec-simplehdmi.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_INTEL) += snd-hda-codec-intelhdmi.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_ATI) += snd-hda-codec-atihdmi.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_NVIDIA) += snd-hda-codec-nvhdmi.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_NVIDIA_MCP) += snd-hda-codec-nvhdmi-mcp.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI_TEGRA) += snd-hda-codec-tegrahdmi.o
