#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-cs46xx-y := cs46xx.o cs46xx_lib.o
snd-cs46xx-$(CONFIG_SND_CS46XX_NEW_DSP) += dsp_spos.o dsp_spos_scb_lib.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_CS46XX) += snd-cs46xx.o
