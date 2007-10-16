#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-emux-synth-objs := emux.o emux_synth.o emux_seq.o emux_nrpn.o \
		       emux_effect.o emux_proc.o emux_hwdep.o soundfont.o \
		       $(if $(CONFIG_SND_SEQUENCER_OSS),emux_oss.o)

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependencies
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += snd-emux-synth.o
obj-$(call sequencer,$(CONFIG_SND_EMU10K1)) += snd-emux-synth.o
