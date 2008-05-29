#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-ac97-codec-y := ac97_codec.o ac97_pcm.o
snd-ac97-codec-$(CONFIG_PROC_FS) += ac97_proc.o

snd-ak4531-codec-objs := ak4531_codec.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_AC97_CODEC) += snd-ac97-codec.o
obj-$(CONFIG_SND_ENS1370) += snd-ak4531-codec.o
