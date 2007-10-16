#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-ac97-codec-objs := ac97_codec.o ac97_pcm.o

ifneq ($(CONFIG_PROC_FS),)
snd-ac97-codec-objs += ac97_proc.o
endif

snd-ak4531-codec-objs := ak4531_codec.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_AC97_CODEC) += snd-ac97-codec.o
obj-$(CONFIG_SND_ENS1370) += snd-ak4531-codec.o

obj-m := $(sort $(obj-m))
