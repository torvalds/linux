# MPC8610 HPCD Machine Support
snd-soc-mpc8610-hpcd-objs := mpc8610_hpcd.o
obj-$(CONFIG_SND_SOC_MPC8610_HPCD) += snd-soc-mpc8610-hpcd.o

# MPC8610 Platform Support
snd-soc-fsl-ssi-objs := fsl_ssi.o
snd-soc-fsl-dma-objs := fsl_dma.o
obj-$(CONFIG_SND_SOC_MPC8610) += snd-soc-fsl-ssi.o snd-soc-fsl-dma.o

# MPC5200 Platform Support
obj-$(CONFIG_SND_MPC52xx_DMA) += mpc5200_dma.o
obj-$(CONFIG_SND_SOC_MPC5200_I2S) += mpc5200_psc_i2s.o
obj-$(CONFIG_SND_SOC_MPC5200_AC97) += mpc5200_psc_ac97.o

# MPC5200 Machine Support
obj-$(CONFIG_SND_MPC52xx_SOC_PCM030) += pcm030-audio-fabric.o
obj-$(CONFIG_SND_MPC52xx_SOC_EFIKA) += efika-audio-fabric.o

