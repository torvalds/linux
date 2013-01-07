snd-soc-core-objs := soc-core.o soc-dapm.o soc-jack.o soc-cache.o soc-utils.o
snd-soc-core-objs += soc-pcm.o soc-compress.o soc-io.o

ifneq ($(CONFIG_SND_SOC_DMAENGINE_PCM),)
snd-soc-core-objs += soc-dmaengine-pcm.o
endif

obj-$(CONFIG_SND_SOC)	+= snd-soc-core.o
obj-$(CONFIG_SND_SOC)	+= codecs/
obj-$(CONFIG_SND_SOC)	+= generic/
obj-$(CONFIG_SND_SOC)	+= atmel/
obj-$(CONFIG_SND_SOC)	+= au1x/
obj-$(CONFIG_SND_SOC)	+= blackfin/
obj-$(CONFIG_SND_SOC)	+= cirrus/
obj-$(CONFIG_SND_SOC)	+= davinci/
obj-$(CONFIG_SND_SOC)	+= dwc/
obj-$(CONFIG_SND_SOC)	+= fsl/
obj-$(CONFIG_SND_SOC)	+= jz4740/
obj-$(CONFIG_SND_SOC)	+= mid-x86/
obj-$(CONFIG_SND_SOC)	+= mxs/
obj-$(CONFIG_SND_SOC)	+= nuc900/
obj-$(CONFIG_SND_SOC)	+= omap/
obj-$(CONFIG_SND_SOC)	+= kirkwood/
obj-$(CONFIG_SND_SOC)	+= pxa/
obj-$(CONFIG_SND_SOC)	+= samsung/
obj-$(CONFIG_SND_SOC)	+= s6000/
obj-$(CONFIG_SND_SOC)	+= sh/
obj-$(CONFIG_SND_SOC)	+= tegra/
obj-$(CONFIG_SND_SOC)	+= txx9/
obj-$(CONFIG_SND_SOC)	+= ux500/
