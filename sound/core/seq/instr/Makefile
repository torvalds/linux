#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
#

snd-ainstr-fm-objs := ainstr_fm.o
snd-ainstr-simple-objs := ainstr_simple.o
snd-ainstr-gf1-objs := ainstr_gf1.o
snd-ainstr-iw-objs := ainstr_iw.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(call sequencer,$(CONFIG_SND_OPL3_LIB)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_OPL4_LIB)) += snd-ainstr-fm.o
obj-$(call sequencer,$(CONFIG_SND_GUS_SYNTH)) += snd-ainstr-gf1.o snd-ainstr-simple.o snd-ainstr-iw.o
obj-$(call sequencer,$(CONFIG_SND_TRIDENT)) += snd-ainstr-simple.o
