#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
#

snd-mixer-oss-objs := mixer_oss.o

snd-pcm-oss-objs := pcm_oss.o pcm_plugin.o \
		    io.o copy.o linear.o mulaw.o route.o rate.o

obj-$(CONFIG_SND_MIXER_OSS) += snd-mixer-oss.o
obj-$(CONFIG_SND_PCM_OSS) += snd-pcm-oss.o
