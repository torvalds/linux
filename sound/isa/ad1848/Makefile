#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-ad1848-lib-objs := ad1848_lib.o
snd-ad1848-objs := ad1848.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_CMI8330) += snd-ad1848-lib.o
obj-$(CONFIG_SND_SGALAXY) += snd-ad1848-lib.o
obj-$(CONFIG_SND_AD1848) += snd-ad1848.o snd-ad1848-lib.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-ad1848-lib.o

obj-m := $(sort $(obj-m))
