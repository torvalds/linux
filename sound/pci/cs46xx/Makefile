#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-cs46xx-objs := cs46xx.o cs46xx_lib.o
ifeq ($(CONFIG_SND_CS46XX_NEW_DSP),y)
  snd-cs46xx-objs += dsp_spos.o dsp_spos_scb_lib.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_CS46XX) += snd-cs46xx.o
