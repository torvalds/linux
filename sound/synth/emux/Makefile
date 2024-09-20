# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-emux-synth-y := emux.o emux_synth.o emux_seq.o emux_nrpn.o \
		       emux_effect.o emux_hwdep.o soundfont.o
snd-emux-synth-$(CONFIG_SND_PROC_FS) += emux_proc.o
ifneq ($(CONFIG_SND_SEQUENCER_OSS),)
snd-emux-synth-y += emux_oss.o
endif

obj-$(CONFIG_SND_SYNTH_EMUX) += snd-emux-synth.o
