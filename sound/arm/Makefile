# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
#

obj-$(CONFIG_SND_ARMAACI)	+= snd-aaci.o
snd-aaci-y			:= aaci.o

obj-$(CONFIG_SND_PXA2XX_LIB)	+= snd-pxa2xx-lib.o
snd-pxa2xx-lib-y		:= pxa2xx-pcm-lib.o
snd-pxa2xx-lib-$(CONFIG_SND_PXA2XX_LIB_AC97)	+= pxa2xx-ac97-lib.o

obj-$(CONFIG_SND_PXA2XX_AC97)	+= snd-pxa2xx-ac97.o
snd-pxa2xx-ac97-y		:= pxa2xx-ac97.o
