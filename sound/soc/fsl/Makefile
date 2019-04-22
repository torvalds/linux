# SPDX-License-Identifier: GPL-2.0
# MPC8610 HPCD Machine Support
snd-soc-mpc8610-hpcd-objs := mpc8610_hpcd.o
obj-$(CONFIG_SND_SOC_MPC8610_HPCD) += snd-soc-mpc8610-hpcd.o

# P1022 DS Machine Support
snd-soc-p1022-ds-objs := p1022_ds.o
obj-$(CONFIG_SND_SOC_P1022_DS) += snd-soc-p1022-ds.o

# P1022 RDK Machine Support
snd-soc-p1022-rdk-objs := p1022_rdk.o
obj-$(CONFIG_SND_SOC_P1022_RDK) += snd-soc-p1022-rdk.o

# Freescale SSI/DMA/SAI/SPDIF Support
snd-soc-fsl-asoc-card-objs := fsl-asoc-card.o
snd-soc-fsl-asrc-objs := fsl_asrc.o fsl_asrc_dma.o
snd-soc-fsl-sai-objs := fsl_sai.o
snd-soc-fsl-ssi-y := fsl_ssi.o
snd-soc-fsl-ssi-$(CONFIG_DEBUG_FS) += fsl_ssi_dbg.o
snd-soc-fsl-spdif-objs := fsl_spdif.o
snd-soc-fsl-esai-objs := fsl_esai.o
snd-soc-fsl-micfil-objs := fsl_micfil.o
snd-soc-fsl-utils-objs := fsl_utils.o
snd-soc-fsl-dma-objs := fsl_dma.o
obj-$(CONFIG_SND_SOC_FSL_ASOC_CARD) += snd-soc-fsl-asoc-card.o
obj-$(CONFIG_SND_SOC_FSL_ASRC) += snd-soc-fsl-asrc.o
obj-$(CONFIG_SND_SOC_FSL_SAI) += snd-soc-fsl-sai.o
obj-$(CONFIG_SND_SOC_FSL_SSI) += snd-soc-fsl-ssi.o
obj-$(CONFIG_SND_SOC_FSL_SPDIF) += snd-soc-fsl-spdif.o
obj-$(CONFIG_SND_SOC_FSL_ESAI) += snd-soc-fsl-esai.o
obj-$(CONFIG_SND_SOC_FSL_MICFIL) += snd-soc-fsl-micfil.o
obj-$(CONFIG_SND_SOC_FSL_UTILS) += snd-soc-fsl-utils.o
obj-$(CONFIG_SND_SOC_POWERPC_DMA) += snd-soc-fsl-dma.o

# MPC5200 Platform Support
obj-$(CONFIG_SND_MPC52xx_DMA) += mpc5200_dma.o
obj-$(CONFIG_SND_SOC_MPC5200_I2S) += mpc5200_psc_i2s.o
obj-$(CONFIG_SND_SOC_MPC5200_AC97) += mpc5200_psc_ac97.o

# MPC5200 Machine Support
obj-$(CONFIG_SND_MPC52xx_SOC_PCM030) += pcm030-audio-fabric.o
obj-$(CONFIG_SND_MPC52xx_SOC_EFIKA) += efika-audio-fabric.o

# i.MX Platform Support
snd-soc-imx-ssi-objs := imx-ssi.o
snd-soc-imx-audmux-objs := imx-audmux.o
obj-$(CONFIG_SND_SOC_IMX_SSI) += snd-soc-imx-ssi.o
obj-$(CONFIG_SND_SOC_IMX_AUDMUX) += snd-soc-imx-audmux.o

obj-$(CONFIG_SND_SOC_IMX_PCM_FIQ) += imx-pcm-fiq.o
obj-$(CONFIG_SND_SOC_IMX_PCM_DMA) += imx-pcm-dma.o

# i.MX Machine Support
snd-soc-eukrea-tlv320-objs := eukrea-tlv320.o
snd-soc-phycore-ac97-objs := phycore-ac97.o
snd-soc-mx27vis-aic32x4-objs := mx27vis-aic32x4.o
snd-soc-wm1133-ev1-objs := wm1133-ev1.o
snd-soc-imx-es8328-objs := imx-es8328.o
snd-soc-imx-sgtl5000-objs := imx-sgtl5000.o
snd-soc-imx-spdif-objs := imx-spdif.o
snd-soc-imx-mc13783-objs := imx-mc13783.o

obj-$(CONFIG_SND_SOC_EUKREA_TLV320) += snd-soc-eukrea-tlv320.o
obj-$(CONFIG_SND_SOC_PHYCORE_AC97) += snd-soc-phycore-ac97.o
obj-$(CONFIG_SND_SOC_MX27VIS_AIC32X4) += snd-soc-mx27vis-aic32x4.o
obj-$(CONFIG_SND_MXC_SOC_WM1133_EV1) += snd-soc-wm1133-ev1.o
obj-$(CONFIG_SND_SOC_IMX_ES8328) += snd-soc-imx-es8328.o
obj-$(CONFIG_SND_SOC_IMX_SGTL5000) += snd-soc-imx-sgtl5000.o
obj-$(CONFIG_SND_SOC_IMX_SPDIF) += snd-soc-imx-spdif.o
obj-$(CONFIG_SND_SOC_IMX_MC13783) += snd-soc-imx-mc13783.o
