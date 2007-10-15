#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-emu10k1-objs := emu10k1.o emu10k1_main.o \
		    irq.o memory.o voice.o emumpu401.o emupcm.o io.o \
		    emuproc.o emumixer.o emufx.o timer.o p16v.o
snd-emu10k1-synth-objs := emu10k1_synth.o emu10k1_callback.o emu10k1_patch.o
snd-emu10k1x-objs := emu10k1x.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(CONFIG_SND_EMU10K1) += snd-emu10k1.o
obj-$(call sequencer,$(CONFIG_SND_EMU10K1)) += snd-emu10k1-synth.o
obj-$(CONFIG_SND_EMU10K1X) += snd-emu10k1x.o
