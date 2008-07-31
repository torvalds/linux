#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-cs4236-lib-objs := cs4236_lib.o
snd-cs4231-objs := cs4231.o
snd-cs4232-objs := cs4232.o
snd-cs4236-objs := cs4236.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_CS4231) += snd-cs4231.o
obj-$(CONFIG_SND_CS4232) += snd-cs4232.o
obj-$(CONFIG_SND_CS4236) += snd-cs4236.o snd-cs4236-lib.o

