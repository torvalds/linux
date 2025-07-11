# SPDX-License-Identifier: GPL-2.0-only

snd-soc-sdca-y := sdca_functions.o sdca_device.o sdca_regmap.o sdca_asoc.o
snd-soc-sdca-hid-y := sdca_hid.o
snd-soc-sdca-irq-y := sdca_interrupts.o

obj-$(CONFIG_SND_SOC_SDCA) += snd-soc-sdca.o
ifdef CONFIG_SND_SOC_SDCA_HID
obj-$(CONFIG_SND_SOC_SDCA) += snd-soc-sdca-hid.o
endif
ifdef CONFIG_SND_SOC_SDCA_IRQ
obj-$(CONFIG_SND_SOC_SDCA) += snd-soc-sdca-irq.o
endif
