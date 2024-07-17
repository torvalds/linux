# SPDX-License-Identifier: GPL-2.0
# AT91 Platform Support
snd-soc-atmel-pcm-pdc-y := atmel-pcm-pdc.o
snd-soc-atmel-pcm-dma-y := atmel-pcm-dma.o
snd-soc-atmel_ssc_dai-y := atmel_ssc_dai.o
snd-soc-atmel-i2s-y := atmel-i2s.o
snd-soc-mchp-i2s-mcc-y := mchp-i2s-mcc.o
snd-soc-mchp-spdiftx-y := mchp-spdiftx.o
snd-soc-mchp-spdifrx-y := mchp-spdifrx.o
snd-soc-mchp-pdmc-y := mchp-pdmc.o

# pdc and dma need to both be built-in if any user of
# ssc is built-in.
ifdef CONFIG_SND_ATMEL_SOC_PDC
obj-$(CONFIG_SND_ATMEL_SOC_SSC) += snd-soc-atmel-pcm-pdc.o
endif
ifdef CONFIG_SND_ATMEL_SOC_DMA
obj-$(CONFIG_SND_ATMEL_SOC_SSC) += snd-soc-atmel-pcm-dma.o
endif
obj-$(CONFIG_SND_ATMEL_SOC_SSC) += snd-soc-atmel_ssc_dai.o
obj-$(CONFIG_SND_ATMEL_SOC_I2S) += snd-soc-atmel-i2s.o
obj-$(CONFIG_SND_MCHP_SOC_I2S_MCC) += snd-soc-mchp-i2s-mcc.o
obj-$(CONFIG_SND_MCHP_SOC_SPDIFTX) += snd-soc-mchp-spdiftx.o
obj-$(CONFIG_SND_MCHP_SOC_SPDIFRX) += snd-soc-mchp-spdifrx.o
obj-$(CONFIG_SND_MCHP_SOC_PDMC) += snd-soc-mchp-pdmc.o

# AT91 Machine Support
snd-soc-sam9g20-wm8731-y := sam9g20_wm8731.o
snd-atmel-soc-wm8904-y := atmel_wm8904.o
snd-soc-sam9x5-wm8731-y := sam9x5_wm8731.o
snd-atmel-soc-classd-y := atmel-classd.o
snd-atmel-soc-pdmic-y := atmel-pdmic.o
snd-atmel-soc-tse850-pcm5142-y := tse850-pcm5142.o
snd-soc-mikroe-proto-y := mikroe-proto.o

obj-$(CONFIG_SND_AT91_SOC_SAM9G20_WM8731) += snd-soc-sam9g20-wm8731.o
obj-$(CONFIG_SND_ATMEL_SOC_WM8904) += snd-atmel-soc-wm8904.o
obj-$(CONFIG_SND_AT91_SOC_SAM9X5_WM8731) += snd-soc-sam9x5-wm8731.o
obj-$(CONFIG_SND_ATMEL_SOC_CLASSD) += snd-atmel-soc-classd.o
obj-$(CONFIG_SND_ATMEL_SOC_PDMIC) += snd-atmel-soc-pdmic.o
obj-$(CONFIG_SND_ATMEL_SOC_TSE850_PCM5142) += snd-atmel-soc-tse850-pcm5142.o
obj-$(CONFIG_SND_SOC_MIKROE_PROTO) += snd-soc-mikroe-proto.o
