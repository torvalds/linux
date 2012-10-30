#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-adlib-objs := adlib.o
snd-als100-objs := als100.o
snd-azt2320-objs := azt2320.o
snd-cmi8328-objs := cmi8328.o
snd-cmi8330-objs := cmi8330.o
snd-es18xx-objs := es18xx.o
snd-opl3sa2-objs := opl3sa2.o
snd-sc6000-objs := sc6000.o
snd-sscape-objs := sscape.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ADLIB) += snd-adlib.o
obj-$(CONFIG_SND_ALS100) += snd-als100.o
obj-$(CONFIG_SND_AZT2320) += snd-azt2320.o
obj-$(CONFIG_SND_CMI8328) += snd-cmi8328.o
obj-$(CONFIG_SND_CMI8330) += snd-cmi8330.o
obj-$(CONFIG_SND_ES18XX) += snd-es18xx.o
obj-$(CONFIG_SND_OPL3SA2) += snd-opl3sa2.o
obj-$(CONFIG_SND_SC6000) += snd-sc6000.o
obj-$(CONFIG_SND_SSCAPE) += snd-sscape.o

obj-$(CONFIG_SND) += ad1816a/ ad1848/ cs423x/ es1688/ galaxy/ gus/ msnd/ opti9xx/ \
		     sb/ wavefront/ wss/
