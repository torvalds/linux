#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-trident-objs := trident.o trident_main.o trident_memory.o
snd-trident-synth-objs := trident_synth.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(CONFIG_SND_TRIDENT) += snd-trident.o
obj-$(call sequencer,$(CONFIG_SND_TRIDENT)) += snd-trident-synth.o
