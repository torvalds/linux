#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-util-mem-objs := util_mem.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(CONFIG_SND_EMU10K1) += snd-util-mem.o
obj-$(CONFIG_SND_TRIDENT) += snd-util-mem.o
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += snd-util-mem.o
obj-$(call sequencer,$(CONFIG_SND)) += emux/
