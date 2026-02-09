# SPDX-License-Identifier: GPL-2.0-only

snd-soc-sdca-y := sdca_functions.o sdca_device.o sdca_function_device.o \
		  sdca_regmap.o sdca_asoc.o sdca_ump.o
snd-soc-sdca-$(CONFIG_SND_SOC_SDCA_HID) += sdca_hid.o
snd-soc-sdca-$(CONFIG_SND_SOC_SDCA_IRQ) += sdca_interrupts.o sdca_jack.o
snd-soc-sdca-$(CONFIG_SND_SOC_SDCA_FDL) += sdca_fdl.o

snd-soc-sdca-class-y := sdca_class.o
snd-soc-sdca-class-function-y := sdca_class_function.o

obj-$(CONFIG_SND_SOC_SDCA) += snd-soc-sdca.o

obj-$(CONFIG_SND_SOC_SDCA_CLASS) += snd-soc-sdca-class.o
obj-$(CONFIG_SND_SOC_SDCA_CLASS_FUNCTION) += snd-soc-sdca-class-function.o
