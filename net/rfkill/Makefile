#
# Makefile for the RF switch subsystem.
#

rfkill-y			+= core.o
rfkill-$(CONFIG_RFKILL_INPUT)	+= input.o
obj-$(CONFIG_RFKILL)		+= rfkill.o
obj-$(CONFIG_RFKILL_REGULATOR)	+= rfkill-regulator.o
obj-$(CONFIG_RFKILL_GPIO)	+= rfkill-gpio.o
