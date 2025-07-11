# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../../common

snd-hda-codec-cs420x-y :=	cs420x.o
snd-hda-codec-cs421x-y :=	cs421x.o
snd-hda-codec-cs8409-y :=	cs8409.o cs8409-tables.o

obj-$(CONFIG_SND_HDA_CODEC_CS420X) += snd-hda-codec-cs420x.o
obj-$(CONFIG_SND_HDA_CODEC_CS421X) += snd-hda-codec-cs421x.o
obj-$(CONFIG_SND_HDA_CODEC_CS8409) += snd-hda-codec-cs8409.o
