# ROCKCHIP Platform Support
snd-soc-rockchip-objs := rk29_pcm.o
ifdef CONFIG_ARCH_RK29
snd-soc-rockchip-i2s-objs := rk29_i2s.o
endif
ifdef CONFIG_ARCH_RK30
snd-soc-rockchip-i2s-objs := rk30_i2s.o
endif
ifdef CONFIG_ARCH_RK2928
snd-soc-rockchip-i2s-objs := rk30_i2s.o
endif
snd-soc-rockchip-spdif-objs := rk29_spdif.o

obj-$(CONFIG_SND_RK29_SOC) += snd-soc-rockchip.o
obj-$(CONFIG_SND_RK29_SOC_I2S) += snd-soc-rockchip-i2s.o
obj-$(CONFIG_SND_RK29_SOC_SPDIF) += snd-soc-rockchip-spdif.o

# ROCKCHIP Machine Support
snd-soc-wm8900-objs := rk29_wm8900.o
snd-soc-rt5621-objs := rk29_rt5621.o
snd-soc-rt5631-objs := rk29_rt5631.o
snd-soc-rt5631-phone-objs := rk29_rt5631_phone.o
snd-soc-rt5625-objs := rk29_rt5625.o
snd-soc-rt3261-objs := rk29_rt3261.o
snd-soc-rt3224-objs := rk29_rt3261.o
snd-soc-cs42l52-objs := rk29_cs42l52.o
snd-soc-aic3111-objs := rk29_aic3111.o
snd-soc-wm8988-objs := rk29_wm8988.o
snd-soc-rk1000-objs := rk29_rk1000codec.o
snd-soc-wm8994-objs := rk29_wm8994.o
snd-soc-hdmi-objs := rk29_hdmi.o
snd-soc-rk610-objs := rk29_jetta_codec.o
snd-soc-aic3262-objs := rk29_aic3262.o
snd-soc-rk2928-objs := rk2928-card.o

obj-$(CONFIG_SND_RK29_SOC_WM8994) += snd-soc-wm8994.o
obj-$(CONFIG_SND_RK29_SOC_WM8988) += snd-soc-wm8988.o
obj-$(CONFIG_SND_RK29_SOC_WM8900) += snd-soc-wm8900.o
obj-$(CONFIG_SND_RK29_SOC_RT5621) += snd-soc-rt5621.o
obj-$(CONFIG_SND_RK29_SOC_RT5631) += snd-soc-rt5631.o
obj-$(CONFIG_SND_RK29_SOC_RT5631_PHONE) += snd-soc-rt5631-phone.o
obj-$(CONFIG_SND_RK29_SOC_RT5625) += snd-soc-rt5625.o
obj-$(CONFIG_SND_RK29_SOC_RT3261) += snd-soc-rt3261.o
obj-$(CONFIG_SND_RK29_SOC_RT3224) += snd-soc-rt3224.o
obj-$(CONFIG_SND_RK29_SOC_RK1000) += snd-soc-rk1000.o
obj-$(CONFIG_SND_RK29_SOC_CS42L52) += snd-soc-cs42l52.o
obj-$(CONFIG_SND_RK29_SOC_AIC3111) += snd-soc-aic3111.o
obj-$(CONFIG_SND_RK29_SOC_AIC3262) += snd-soc-aic3262.o
obj-$(CONFIG_SND_RK29_SOC_HDMI) += snd-soc-hdmi.o
obj-$(CONFIG_SND_RK29_SOC_RK610) += snd-soc-rk610.o
obj-$(CONFIG_SND_RK_SOC_RK2928) += snd-soc-rk2928.o
