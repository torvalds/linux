#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-opl4-lib-objs := opl4_lib.o opl4_mixer.o opl4_proc.o
snd-opl4-synth-objs := opl4_seq.o opl4_synth.o yrw801.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

obj-$(CONFIG_SND_OPL4_LIB) += snd-opl4-lib.o
obj-$(call sequencer,$(CONFIG_SND_OPL4_LIB)) += snd-opl4-synth.o
