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
snd-soc-fsl-audmix-objs := fsl_audmix.o
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
snd-soc-fsl-mqs-objs := fsl_mqs.o
snd-soc-fsl-easrc-objs := fsl_easrc.o
snd-soc-fsl-xcvr-objs := fsl_xcvr.o
snd-soc-fsl-aud2htx-objs := fsl_aud2htx.o
snd-soc-fsl-rpmsg-objs := fsl_rpmsg.o

obj-$(CONFIG_SND_SOC_FSL_AUDMIX) += snd-soc-fsl-audmix.o
obj-$(CONFIG_SND_SOC_FSL_ASOC_CARD) += snd-soc-fsl-asoc-card.o
obj-$(CONFIG_SND_SOC_FSL_ASRC) += snd-soc-fsl-asrc.o
obj-$(CONFIG_SND_SOC_FSL_SAI) += snd-soc-fsl-sai.o
obj-$(CONFIG_SND_SOC_FSL_SSI) += snd-soc-fsl-ssi.o
obj-$(CONFIG_SND_SOC_FSL_SPDIF) += snd-soc-fsl-spdif.o
obj-$(CONFIG_SND_SOC_FSL_ESAI) += snd-soc-fsl-esai.o
obj-$(CONFIG_SND_SOC_FSL_MICFIL) += snd-soc-fsl-micfil.o
obj-$(CONFIG_SND_SOC_FSL_UTILS) += snd-soc-fsl-utils.o
obj-$(CONFIG_SND_SOC_FSL_MQS) += snd-soc-fsl-mqs.o
obj-$(CONFIG_SND_SOC_FSL_EASRC) += snd-soc-fsl-easrc.o
obj-$(CONFIG_SND_SOC_POWERPC_DMA) += snd-soc-fsl-dma.o
obj-$(CONFIG_SND_SOC_FSL_XCVR) += snd-soc-fsl-xcvr.o
obj-$(CONFIG_SND_SOC_FSL_AUD2HTX) += snd-soc-fsl-aud2htx.o
obj-$(CONFIG_SND_SOC_FSL_RPMSG) += snd-soc-fsl-rpmsg.o

# MPC5200 Platform Support
obj-$(CONFIG_SND_MPC52xx_DMA) += mpc5200_dma.o
obj-$(CONFIG_SND_SOC_MPC5200_I2S) += mpc5200_psc_i2s.o
obj-$(CONFIG_SND_SOC_MPC5200_AC97) += mpc5200_psc_ac97.o

# MPC5200 Machine Support
obj-$(CONFIG_SND_MPC52xx_SOC_PCM030) += pcm030-audio-fabric.o
obj-$(CONFIG_SND_MPC52xx_SOC_EFIKA) += efika-audio-fabric.o

# i.MX Platform Support
snd-soc-imx-audmux-objs := imx-audmux.o
obj-$(CONFIG_SND_SOC_IMX_AUDMUX) += snd-soc-imx-audmux.o

obj-$(CONFIG_SND_SOC_IMX_PCM_FIQ) += imx-pcm-fiq.o
obj-$(CONFIG_SND_SOC_IMX_PCM_DMA) += imx-pcm-dma.o
obj-$(CONFIG_SND_SOC_IMX_AUDIO_RPMSG) += imx-audio-rpmsg.o
obj-$(CONFIG_SND_SOC_IMX_PCM_RPMSG) += imx-pcm-rpmsg.o

# i.MX Machine Support
snd-soc-eukrea-tlv320-objs := eukrea-tlv320.o
snd-soc-imx-es8328-objs := imx-es8328.o
snd-soc-imx-sgtl5000-objs := imx-sgtl5000.o
snd-soc-imx-spdif-objs := imx-spdif.o
snd-soc-imx-audmix-objs := imx-audmix.o
snd-soc-imx-hdmi-objs := imx-hdmi.o
snd-soc-imx-rpmsg-objs := imx-rpmsg.o

obj-$(CONFIG_SND_SOC_EUKREA_TLV320) += snd-soc-eukrea-tlv320.o
obj-$(CONFIG_SND_SOC_IMX_ES8328) += snd-soc-imx-es8328.o
obj-$(CONFIG_SND_SOC_IMX_SGTL5000) += snd-soc-imx-sgtl5000.o
obj-$(CONFIG_SND_SOC_IMX_SPDIF) += snd-soc-imx-spdif.o
obj-$(CONFIG_SND_SOC_IMX_AUDMIX) += snd-soc-imx-audmix.o
obj-$(CONFIG_SND_SOC_IMX_HDMI) += snd-soc-imx-hdmi.o
obj-$(CONFIG_SND_SOC_IMX_RPMSG) += snd-soc-imx-rpmsg.o
