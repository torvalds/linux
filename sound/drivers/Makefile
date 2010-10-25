#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-dummy-objs := dummy.o
snd-aloop-objs := aloop.o
snd-mtpav-objs := mtpav.o
snd-mts64-objs := mts64.o
snd-portman2x4-objs := portman2x4.o
snd-serial-u16550-objs := serial-u16550.o
snd-virmidi-objs := virmidi.o
snd-ml403-ac97cr-objs := ml403-ac97cr.o pcm-indirect2.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_DUMMY) += snd-dummy.o
obj-$(CONFIG_SND_ALOOP) += snd-aloop.o
obj-$(CONFIG_SND_VIRMIDI) += snd-virmidi.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-serial-u16550.o
obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o
obj-$(CONFIG_SND_MTS64) += snd-mts64.o
obj-$(CONFIG_SND_PORTMAN2X4) += snd-portman2x4.o
obj-$(CONFIG_SND_ML403_AC97CR) += snd-ml403-ac97cr.o

obj-$(CONFIG_SND) += opl3/ opl4/ mpu401/ vx/ pcsp/
