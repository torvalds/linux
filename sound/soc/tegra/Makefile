# Tegra platform Support
snd-soc-tegra-das-objs := tegra_das.o
snd-soc-tegra-pcm-objs := tegra_pcm.o
snd-soc-tegra-i2s-objs := tegra_i2s.o
snd-soc-tegra-spdif-objs := tegra_spdif.o
snd-soc-tegra-utils-objs += tegra_asoc_utils.o

obj-$(CONFIG_SND_SOC_TEGRA) += snd-soc-tegra-utils.o
obj-$(CONFIG_SND_SOC_TEGRA) += snd-soc-tegra-das.o
obj-$(CONFIG_SND_SOC_TEGRA) += snd-soc-tegra-pcm.o
obj-$(CONFIG_SND_SOC_TEGRA_I2S) += snd-soc-tegra-i2s.o
obj-$(CONFIG_SND_SOC_TEGRA_SPDIF) += snd-soc-tegra-spdif.o

# Tegra machine Support
snd-soc-tegra-wm8903-objs := tegra_wm8903.o
snd-soc-tegra-trimslice-objs := trimslice.o

obj-$(CONFIG_SND_SOC_TEGRA_WM8903) += snd-soc-tegra-wm8903.o
obj-$(CONFIG_SND_SOC_TEGRA_TRIMSLICE) += snd-soc-tegra-trimslice.o
