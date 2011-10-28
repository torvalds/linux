# Au1200/Au1550 PSC audio
snd-soc-au1xpsc-dbdma-objs := dbdma2.o
snd-soc-au1xpsc-i2s-objs := psc-i2s.o
snd-soc-au1xpsc-ac97-objs := psc-ac97.o

# Au1000/1500/1100 Audio units
snd-soc-au1x-dma-objs := dma.o
snd-soc-au1x-ac97c-objs := ac97c.o
snd-soc-au1x-i2sc-objs := i2sc.o

obj-$(CONFIG_SND_SOC_AU1XPSC) += snd-soc-au1xpsc-dbdma.o
obj-$(CONFIG_SND_SOC_AU1XPSC_I2S) += snd-soc-au1xpsc-i2s.o
obj-$(CONFIG_SND_SOC_AU1XPSC_AC97) += snd-soc-au1xpsc-ac97.o
obj-$(CONFIG_SND_SOC_AU1XAUDIO) += snd-soc-au1x-dma.o
obj-$(CONFIG_SND_SOC_AU1XAC97C) += snd-soc-au1x-ac97c.o
obj-$(CONFIG_SND_SOC_AU1XI2SC) += snd-soc-au1x-i2sc.o

# Boards
snd-soc-db1000-objs := db1000.o
snd-soc-db1200-objs := db1200.o

obj-$(CONFIG_SND_SOC_DB1000) += snd-soc-db1000.o
obj-$(CONFIG_SND_SOC_DB1200) += snd-soc-db1200.o
