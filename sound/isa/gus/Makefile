#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-gus-lib-objs := gus_main.o \
		    gus_io.o gus_irq.o gus_timer.o \
		    gus_mem.o gus_mem_proc.o gus_dram.o gus_dma.o gus_volume.o \
		    gus_pcm.o gus_mixer.o \
		    gus_uart.o \
		    gus_reset.o

snd-gusclassic-objs := gusclassic.o
snd-gusextreme-objs := gusextreme.o
snd-gusmax-objs := gusmax.o
snd-interwave-objs := interwave.o
snd-interwave-stb-objs := interwave-stb.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_GUSCLASSIC) += snd-gusclassic.o snd-gus-lib.o
obj-$(CONFIG_SND_GUSMAX) += snd-gusmax.o snd-gus-lib.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-gusextreme.o snd-gus-lib.o
obj-$(CONFIG_SND_INTERWAVE) += snd-interwave.o snd-gus-lib.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-interwave-stb.o snd-gus-lib.o
