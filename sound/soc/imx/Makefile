# i.MX Platform Support
snd-soc-imx-objs := imx-ssi.o imx-pcm-fiq.o

ifdef CONFIG_MACH_MX27
snd-soc-imx-objs += imx-pcm-dma-mx2.o
endif

obj-$(CONFIG_SND_IMX_SOC) += snd-soc-imx.o

# i.MX Machine Support
snd-soc-phycore-ac97-objs := phycore-ac97.o
snd-soc-wm1133-ev1-objs := wm1133-ev1.o

obj-$(CONFIG_SND_SOC_PHYCORE_AC97) += snd-soc-phycore-ac97.o
obj-$(CONFIG_SND_MXC_SOC_WM1133_EV1) += snd-soc-wm1133-ev1.o
