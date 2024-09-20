# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-emu10k1-y := emu10k1.o emu10k1_main.o \
		    irq.o memory.o voice.o emumpu401.o emupcm.o io.o \
		    emumixer.o emufx.o timer.o p16v.o
snd-emu10k1-$(CONFIG_SND_PROC_FS) += emuproc.o
snd-emu10k1-synth-y := emu10k1_synth.o emu10k1_callback.o emu10k1_patch.o
snd-emu10k1x-y := emu10k1x.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_EMU10K1) += snd-emu10k1.o
obj-$(CONFIG_SND_EMU10K1_SEQ) += snd-emu10k1-synth.o
obj-$(CONFIG_SND_EMU10K1X) += snd-emu10k1x.o
