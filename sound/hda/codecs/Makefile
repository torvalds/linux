# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../common

snd-hda-codec-generic-y :=	generic.o
snd-hda-codec-cmedia-y :=	cmedia.o
snd-hda-codec-cm9825-y :=	cm9825.o
snd-hda-codec-analog-y :=	analog.o
snd-hda-codec-ca0110-y :=	ca0110.o
snd-hda-codec-ca0132-y :=	ca0132.o
snd-hda-codec-cmedia-y :=	cmedia.o
snd-hda-codec-conexant-y :=	conexant.o
snd-hda-codec-idt-y :=		sigmatel.o
snd-hda-codec-senarytech-y :=	senarytech.o
snd-hda-codec-si3054-y :=	si3054.o
snd-hda-codec-via-y :=		via.o

obj-y += cirrus/
obj-y += hdmi/
obj-y += realtek/
obj-y += side-codecs/

# codec drivers
obj-$(CONFIG_SND_HDA_GENERIC) += snd-hda-codec-generic.o
obj-$(CONFIG_SND_HDA_CODEC_CMEDIA) += snd-hda-codec-cmedia.o
obj-$(CONFIG_SND_HDA_CODEC_CM9825) += snd-hda-codec-cm9825.o
obj-$(CONFIG_SND_HDA_CODEC_ANALOG) += snd-hda-codec-analog.o
obj-$(CONFIG_SND_HDA_CODEC_CA0110) += snd-hda-codec-ca0110.o
obj-$(CONFIG_SND_HDA_CODEC_CA0132) += snd-hda-codec-ca0132.o
obj-$(CONFIG_SND_HDA_CODEC_CMEDIA) += snd-hda-codec-cmedia.o
obj-$(CONFIG_SND_HDA_CODEC_CONEXANT) += snd-hda-codec-conexant.o
obj-$(CONFIG_SND_HDA_CODEC_SIGMATEL) += snd-hda-codec-idt.o
obj-$(CONFIG_SND_HDA_CODEC_SENARYTECH) += snd-hda-codec-senarytech.o
obj-$(CONFIG_SND_HDA_CODEC_SI3054) += snd-hda-codec-si3054.o
obj-$(CONFIG_SND_HDA_CODEC_VIA) += snd-hda-codec-via.o
