#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-ice17xx-ak4xxx-objs := ak4xxx.o
snd-ice1712-objs := ice1712.o delta.o hoontech.o ews.o
snd-ice1724-objs := ice1724.o amp.o revo.o aureon.o vt1720_mobo.o pontis.o prodigy192.o prodigy_hifi.o juli.o phase.o wtm.o se.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ICE1712) += snd-ice1712.o snd-ice17xx-ak4xxx.o
obj-$(CONFIG_SND_ICE1724) += snd-ice1724.o snd-ice17xx-ak4xxx.o
