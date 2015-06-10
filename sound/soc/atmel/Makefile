# AT91 Platform Support
snd-soc-atmel-pcm-$(CONFIG_SND_ATMEL_SOC_PDC) := atmel-pcm-pdc.o
snd-soc-atmel-pcm-$(CONFIG_SND_ATMEL_SOC_DMA) += atmel-pcm-dma.o
snd-soc-atmel_ssc_dai-objs := atmel_ssc_dai.o $(snd-soc-atmel-pcm-y)

obj-$(CONFIG_SND_ATMEL_SOC_SSC) += snd-soc-atmel_ssc_dai.o

# AT91 Machine Support
snd-soc-sam9g20-wm8731-objs := sam9g20_wm8731.o
snd-atmel-soc-wm8904-objs := atmel_wm8904.o
snd-soc-sam9x5-wm8731-objs := sam9x5_wm8731.o

obj-$(CONFIG_SND_AT91_SOC_SAM9G20_WM8731) += snd-soc-sam9g20-wm8731.o
obj-$(CONFIG_SND_ATMEL_SOC_WM8904) += snd-atmel-soc-wm8904.o
obj-$(CONFIG_SND_AT91_SOC_SAM9X5_WM8731) += snd-soc-sam9x5-wm8731.o
