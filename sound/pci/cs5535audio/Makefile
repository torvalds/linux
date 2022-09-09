# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for cs5535audio
#

snd-cs5535audio-y := cs5535audio.o cs5535audio_pcm.o
snd-cs5535audio-$(CONFIG_PM_SLEEP) += cs5535audio_pm.o
snd-cs5535audio-$(CONFIG_OLPC) += cs5535audio_olpc.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_CS5535AUDIO) += snd-cs5535audio.o
