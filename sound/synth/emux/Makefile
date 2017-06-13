#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-emux-synth-objs := emux.o emux_synth.o emux_seq.o emux_nrpn.o \
		       emux_effect.o emux_hwdep.o soundfont.o
snd-emux-synth-$(CONFIG_SND_PROC_FS) += emux_proc.o
snd-emux-synth-$(CONFIG_SND_SEQUENCER_OSS) += emux_oss.o

obj-$(CONFIG_SND_SYNTH_EMUX) += snd-emux-synth.o
