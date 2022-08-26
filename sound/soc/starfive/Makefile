# starfive Platform Support
obj-$(CONFIG_SND_SOC_STARFIVE_PWMDAC) += pwmdac.o
pwmdac-y := starfive_pwmdac.o starfive_pwmdac_transmitter.o
pwmdac-$(CONFIG_SND_SOC_STARFIVE_PWMDAC_PCM) += starfive_pwmdac_pcm.o

snd-soc-i2srx-master-objs := i2srx-master.o
obj-$(CONFIG_SND_SOC_STARFIVE_I2S_MASTER) += snd-soc-i2srx-master.o

obj-$(CONFIG_SND_SOC_STARFIVE_PDM) += starfive_pdm.o
obj-$(CONFIG_SND_SOC_STARFIVE_TDM) += starfive_tdm.o

obj-$(CONFIG_SND_SOC_STARFIVE_SPDIF) += spdif.o
spdif-y := starfive_spdif.o
spdif-$(CONFIG_SND_SOC_STARFIVE_SPDIF_PCM) += starfive_spdif_pcm.o
