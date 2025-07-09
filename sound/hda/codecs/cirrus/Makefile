# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../../common

snd-hda-codec-cirrus-y :=	cirrus.o
snd-hda-codec-cs8409-y :=	cs8409.o cs8409-tables.o

obj-$(CONFIG_SND_HDA_CODEC_CIRRUS) += snd-hda-codec-cirrus.o
obj-$(CONFIG_SND_HDA_CODEC_CS8409) += snd-hda-codec-cs8409.o
