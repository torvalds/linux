#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-cs4231-lib-objs := cs4231_lib.o
snd-cs4236-lib-objs := cs4236_lib.o
snd-cs4231-objs := cs4231.o
snd-cs4232-objs := cs4232.o
snd-cs4236-objs := cs4236.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_AZT2320) += snd-cs4231-lib.o
obj-$(CONFIG_SND_MIRO) += snd-cs4231-lib.o
obj-$(CONFIG_SND_OPL3SA2) += snd-cs4231-lib.o
obj-$(CONFIG_SND_CS4231) += snd-cs4231.o snd-cs4231-lib.o
obj-$(CONFIG_SND_CS4232) += snd-cs4232.o snd-cs4231-lib.o
obj-$(CONFIG_SND_CS4236) += snd-cs4236.o snd-cs4236-lib.o snd-cs4231-lib.o
obj-$(CONFIG_SND_GUSMAX) += snd-cs4231-lib.o
obj-$(CONFIG_SND_INTERWAVE) += snd-cs4231-lib.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-cs4231-lib.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-cs4231-lib.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-cs4231-lib.o
obj-$(CONFIG_SND_SSCAPE) += snd-cs4231-lib.o

obj-m := $(sort $(obj-m))
