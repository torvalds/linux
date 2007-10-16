#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-sb-common-objs := sb_common.o sb_mixer.o
snd-sb8-dsp-objs := sb8_main.o sb8_midi.o
snd-sb16-dsp-objs := sb16_main.o
snd-sb16-csp-objs := sb16_csp.o
snd-sb8-objs := sb8.o
snd-sb16-objs := sb16.o
snd-sbawe-objs := sbawe.o emu8000.o
snd-emu8000-synth-objs := emu8000_synth.o emu8000_callback.o emu8000_patch.o emu8000_pcm.o
snd-es968-objs := es968.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

# Toplevel Module Dependency
obj-$(CONFIG_SND_SB_COMMON) += snd-sb-common.o
obj-$(CONFIG_SND_SB16_DSP) += snd-sb16-dsp.o
obj-$(CONFIG_SND_SB8_DSP) += snd-sb8-dsp.o
obj-$(CONFIG_SND_SB8) += snd-sb8.o
obj-$(CONFIG_SND_SB16) += snd-sb16.o
obj-$(CONFIG_SND_SBAWE) += snd-sbawe.o
obj-$(CONFIG_SND_ES968) += snd-es968.o
ifeq ($(CONFIG_SND_SB16_CSP),y)
  obj-$(CONFIG_SND_SB16) += snd-sb16-csp.o
  obj-$(CONFIG_SND_SBAWE) += snd-sb16-csp.o
endif
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += snd-emu8000-synth.o

obj-m := $(sort $(obj-m))
