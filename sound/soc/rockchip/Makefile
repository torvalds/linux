# SPDX-License-Identifier: GPL-2.0
# ROCKCHIP Platform Support
snd-soc-rockchip-i2s-objs := rockchip_i2s.o
snd-soc-rockchip-i2s-tdm-objs := rockchip_i2s_tdm.o
snd-soc-rockchip-pdm-objs := rockchip_pdm.o
snd-soc-rockchip-spdif-objs := rockchip_spdif.o
snd-soc-rockchip-spdifrx-objs := rockchip_spdifrx.o
snd-soc-rockchip-vad-objs := rockchip_vad.o
ifdef CONFIG_THUMB2_KERNEL
snd-soc-rockchip-vad-$(CONFIG_THUMB2_KERNEL) += vad_preprocess_thumb.o
else
snd-soc-rockchip-vad-$(CONFIG_ARM64) += vad_preprocess_arm64.o
snd-soc-rockchip-vad-$(CONFIG_ARM) += vad_preprocess_arm.o
endif

obj-$(CONFIG_SND_SOC_ROCKCHIP_I2S) += snd-soc-rockchip-i2s.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_I2S_TDM) += snd-soc-rockchip-i2s-tdm.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_PDM) += snd-soc-rockchip-pdm.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_SPDIF) += snd-soc-rockchip-spdif.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_SPDIFRX) += snd-soc-rockchip-spdifrx.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_VAD) += snd-soc-rockchip-vad.o

snd-soc-rockchip-hdmi-objs := rockchip_hdmi.o
snd-soc-rockchip-max98090-objs := rockchip_max98090.o
snd-soc-rockchip-multicodecs-objs := rockchip_multicodecs.o
snd-soc-rockchip-rt5645-objs := rockchip_rt5645.o
snd-soc-rk3288-hdmi-analog-objs := rk3288_hdmi_analog.o
snd-soc-rk3399-gru-sound-objs := rk3399_gru_sound.o

obj-$(CONFIG_SND_SOC_ROCKCHIP_HDMI) += snd-soc-rockchip-hdmi.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_MAX98090) += snd-soc-rockchip-max98090.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_MULTICODECS) += snd-soc-rockchip-multicodecs.o
obj-$(CONFIG_SND_SOC_ROCKCHIP_RT5645) += snd-soc-rockchip-rt5645.o
obj-$(CONFIG_SND_SOC_RK3288_HDMI_ANALOG) += snd-soc-rk3288-hdmi-analog.o
obj-$(CONFIG_SND_SOC_RK3399_GRU_SOUND) += snd-soc-rk3399-gru-sound.o
