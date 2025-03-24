# SPDX-License-Identifier: GPL-2.0
snd-soc-core-y := soc-core.o soc-dapm.o soc-jack.o soc-utils.o soc-dai.o soc-component.o
snd-soc-core-y += soc-pcm.o soc-devres.o soc-ops.o soc-link.o soc-card.o
snd-soc-core-$(CONFIG_SND_SOC_COMPRESS) += soc-compress.o

ifneq ($(CONFIG_SND_SOC_TOPOLOGY),)
snd-soc-core-y += soc-topology.o
endif

ifneq ($(CONFIG_SND_SOC_TOPOLOGY_KUNIT_TEST),)
# snd-soc-test-y := soc-topology-test.o
obj-$(CONFIG_SND_SOC_TOPOLOGY_KUNIT_TEST) += soc-topology-test.o
endif

ifneq ($(CONFIG_SND_SOC_CARD_KUNIT_TEST),)
obj-$(CONFIG_SND_SOC_CARD_KUNIT_TEST) += soc-card-test.o
endif

ifneq ($(CONFIG_SND_SOC_UTILS_KUNIT_TEST),)
# snd-soc-test-y := soc-utils-test.o
obj-$(CONFIG_SND_SOC_UTILS_KUNIT_TEST) += soc-utils-test.o
endif

ifneq ($(CONFIG_SND_SOC_GENERIC_DMAENGINE_PCM),)
snd-soc-core-y += soc-generic-dmaengine-pcm.o
endif

ifneq ($(CONFIG_SND_SOC_AC97_BUS),)
snd-soc-core-y += soc-ac97.o
endif

ifneq ($(CONFIG_SND_SOC_ACPI),)
snd-soc-acpi-y := soc-acpi.o
endif

obj-$(CONFIG_SND_SOC_ACPI) += snd-soc-acpi.o

obj-$(CONFIG_SND_SOC)	+= snd-soc-core.o
obj-$(CONFIG_SND_SOC)	+= codecs/
obj-$(CONFIG_SND_SOC)	+= generic/
obj-$(CONFIG_SND_SOC)	+= apple/
obj-$(CONFIG_SND_SOC)	+= adi/
obj-$(CONFIG_SND_SOC)	+= amd/
obj-$(CONFIG_SND_SOC)	+= atmel/
obj-$(CONFIG_SND_SOC)	+= au1x/
obj-$(CONFIG_SND_SOC)	+= bcm/
obj-$(CONFIG_SND_SOC)	+= cirrus/
obj-$(CONFIG_SND_SOC)	+= dwc/
obj-$(CONFIG_SND_SOC)	+= fsl/
obj-$(CONFIG_SND_SOC)	+= google/
obj-$(CONFIG_SND_SOC)	+= hisilicon/
obj-$(CONFIG_SND_SOC)	+= jz4740/
obj-$(CONFIG_SND_SOC)	+= loongson/
obj-$(CONFIG_SND_SOC)	+= img/
obj-$(CONFIG_SND_SOC)	+= intel/
obj-$(CONFIG_SND_SOC)	+= mediatek/
obj-$(CONFIG_SND_SOC)	+= meson/
obj-$(CONFIG_SND_SOC)	+= mxs/
obj-$(CONFIG_SND_SOC)	+= kirkwood/
obj-$(CONFIG_SND_SOC)	+= pxa/
obj-$(CONFIG_SND_SOC)	+= qcom/
obj-$(CONFIG_SND_SOC)	+= renesas/
obj-$(CONFIG_SND_SOC)	+= rockchip/
obj-$(CONFIG_SND_SOC)	+= samsung/
obj-$(CONFIG_SND_SOC)	+= sdca/
obj-$(CONFIG_SND_SOC)	+= sof/
obj-$(CONFIG_SND_SOC)	+= spear/
obj-$(CONFIG_SND_SOC)	+= sprd/
obj-$(CONFIG_SND_SOC)	+= starfive/
obj-$(CONFIG_SND_SOC)	+= sti/
obj-$(CONFIG_SND_SOC)	+= stm/
obj-$(CONFIG_SND_SOC)	+= sunxi/
obj-$(CONFIG_SND_SOC)	+= tegra/
obj-$(CONFIG_SND_SOC)	+= ti/
obj-$(CONFIG_SND_SOC)	+= uniphier/
obj-$(CONFIG_SND_SOC)	+= ux500/
obj-$(CONFIG_SND_SOC)	+= xilinx/
obj-$(CONFIG_SND_SOC)	+= xtensa/
obj-$(CONFIG_SND_SOC)	+= sdw_utils/
