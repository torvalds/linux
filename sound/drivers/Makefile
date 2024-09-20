# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-dummy-y := dummy.o
snd-aloop-y := aloop.o
snd-mtpav-y := mtpav.o
snd-mts64-y := mts64.o
snd-pcmtest-y := pcmtest.o
snd-portman2x4-y := portman2x4.o
snd-serial-u16550-y := serial-u16550.o
snd-serial-generic-y := serial-generic.o
snd-virmidi-y := virmidi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_DUMMY) += snd-dummy.o
obj-$(CONFIG_SND_ALOOP) += snd-aloop.o
obj-$(CONFIG_SND_VIRMIDI) += snd-virmidi.o
obj-$(CONFIG_SND_PCMTEST) += snd-pcmtest.o
obj-$(CONFIG_SND_SERIAL_U16550) += snd-serial-u16550.o
obj-$(CONFIG_SND_SERIAL_GENERIC) += snd-serial-generic.o
obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o
obj-$(CONFIG_SND_MTS64) += snd-mts64.o
obj-$(CONFIG_SND_PORTMAN2X4) += snd-portman2x4.o

obj-$(CONFIG_SND) += opl3/ opl4/ mpu401/ vx/ pcsp/
