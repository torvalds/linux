#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-opl3-lib-objs := opl3_lib.o opl3_synth.o
snd-opl3-synth-objs := opl3_seq.o opl3_midi.o opl3_drums.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
snd-opl3-synth-objs += opl3_oss.o
endif

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

obj-$(CONFIG_SND_OPL3_LIB) += snd-opl3-lib.o
obj-$(CONFIG_SND_OPL4_LIB) += snd-opl3-lib.o
obj-$(call sequencer,$(CONFIG_SND_OPL3_LIB)) += snd-opl3-synth.o
