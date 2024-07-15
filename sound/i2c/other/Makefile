# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 2003 by Jaroslav Kysela <perex@perex.cz>
#

snd-ak4114-y := ak4114.o
snd-ak4117-y := ak4117.o
snd-ak4113-y := ak4113.o
snd-ak4xxx-adda-y := ak4xxx-adda.o
snd-pt2258-y := pt2258.o

# Module Dependency
obj-$(CONFIG_SND_PDAUDIOCF) += snd-ak4117.o
obj-$(CONFIG_SND_ICE1712) += snd-ak4xxx-adda.o
obj-$(CONFIG_SND_ICE1724) += snd-ak4114.o snd-ak4113.o snd-ak4xxx-adda.o snd-pt2258.o
