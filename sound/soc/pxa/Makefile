# SPDX-License-Identifier: GPL-2.0
# PXA Platform Support
snd-soc-pxa2xx-objs := pxa2xx-pcm.o
snd-soc-pxa2xx-ac97-objs := pxa2xx-ac97.o
snd-soc-pxa2xx-i2s-objs := pxa2xx-i2s.o
snd-soc-pxa-ssp-objs := pxa-ssp.o
snd-soc-mmp-sspa-objs := mmp-sspa.o

obj-$(CONFIG_SND_PXA2XX_SOC) += snd-soc-pxa2xx.o
obj-$(CONFIG_SND_PXA2XX_SOC_AC97) += snd-soc-pxa2xx-ac97.o
obj-$(CONFIG_SND_PXA2XX_SOC_I2S) += snd-soc-pxa2xx-i2s.o
obj-$(CONFIG_SND_PXA_SOC_SSP) += snd-soc-pxa-ssp.o
obj-$(CONFIG_SND_MMP_SOC_SSPA) += snd-soc-mmp-sspa.o

# PXA Machine Support
snd-soc-spitz-objs := spitz.o
obj-$(CONFIG_SND_PXA2XX_SOC_SPITZ) += snd-soc-spitz.o
