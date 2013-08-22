## DMA engines
snd-soc-dma-sh7760-objs	:= dma-sh7760.o
obj-$(CONFIG_SND_SOC_PCM_SH7760)	+= snd-soc-dma-sh7760.o

## audio units found on some SH-4
snd-soc-hac-objs	:= hac.o
snd-soc-ssi-objs	:= ssi.o
snd-soc-fsi-objs	:= fsi.o
snd-soc-siu-objs	:= siu_pcm.o siu_dai.o
obj-$(CONFIG_SND_SOC_SH4_HAC)	+= snd-soc-hac.o
obj-$(CONFIG_SND_SOC_SH4_SSI)	+= snd-soc-ssi.o
obj-$(CONFIG_SND_SOC_SH4_FSI)	+= snd-soc-fsi.o
obj-$(CONFIG_SND_SOC_SH4_SIU)	+= snd-soc-siu.o

## audio units for R-Car
obj-$(CONFIG_SND_SOC_RCAR)	+= rcar/

## boards
snd-soc-sh7760-ac97-objs	:= sh7760-ac97.o
snd-soc-migor-objs		:= migor.o

obj-$(CONFIG_SND_SH7760_AC97)	+= snd-soc-sh7760-ac97.o
obj-$(CONFIG_SND_SIU_MIGOR)	+= snd-soc-migor.o
