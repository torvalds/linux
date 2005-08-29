#
# Makefile for ALSA
# Copyright (c) 1999,2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-objs     := sound.o init.o memory.o info.o control.o misc.o \
                device.o wrappers.o
ifeq ($(CONFIG_ISA_DMA_API),y)
snd-objs     += isadma.o
endif
ifeq ($(CONFIG_SND_OSSEMUL),y)
snd-objs     += sound_oss.o info_oss.o
endif

snd-pcm-objs := pcm.o pcm_native.o pcm_lib.o pcm_timer.o pcm_misc.o \
		pcm_memory.o

snd-page-alloc-objs := memalloc.o sgbuf.o

snd-rawmidi-objs  := rawmidi.o
snd-timer-objs    := timer.o
snd-rtctimer-objs := rtctimer.o
snd-hwdep-objs    := hwdep.o

obj-$(CONFIG_SND) 		+= snd.o
obj-$(CONFIG_SND_HWDEP)		+= snd-hwdep.o
obj-$(CONFIG_SND_TIMER)		+= snd-timer.o
obj-$(CONFIG_SND_RTCTIMER)	+= snd-rtctimer.o
obj-$(CONFIG_SND_PCM)		+= snd-pcm.o snd-page-alloc.o
obj-$(CONFIG_SND_RAWMIDI)	+= snd-rawmidi.o

obj-$(CONFIG_SND_OSSEMUL)	+= oss/
obj-$(CONFIG_SND_SEQUENCER)	+= seq/
