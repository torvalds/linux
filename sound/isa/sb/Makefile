# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-sb-common-y := sb_common.o sb_mixer.o
snd-sb8-dsp-y := sb8_main.o sb8_midi.o
snd-sb16-dsp-y := sb16_main.o
snd-sb16-csp-y := sb16_csp.o
snd-sb8-y := sb8.o
snd-sb16-y := sb16.o
snd-sbawe-y := sbawe.o emu8000.o
snd-emu8000-synth-y := emu8000_synth.o emu8000_callback.o emu8000_patch.o emu8000_pcm.o
snd-jazz16-y := jazz16.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_SB_COMMON) += snd-sb-common.o
obj-$(CONFIG_SND_SB16_DSP) += snd-sb16-dsp.o
obj-$(CONFIG_SND_SB8_DSP) += snd-sb8-dsp.o
obj-$(CONFIG_SND_SB8) += snd-sb8.o
obj-$(CONFIG_SND_SB16) += snd-sb16.o
obj-$(CONFIG_SND_SBAWE) += snd-sbawe.o
obj-$(CONFIG_SND_JAZZ16) += snd-jazz16.o
ifeq ($(CONFIG_SND_SB16_CSP),y)
  obj-$(CONFIG_SND_SB16) += snd-sb16-csp.o
  obj-$(CONFIG_SND_SBAWE) += snd-sb16-csp.o
endif
obj-$(CONFIG_SND_SBAWE_SEQ) += snd-emu8000-synth.o
