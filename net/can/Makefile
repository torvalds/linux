#
#  Makefile for the Linux Controller Area Network core.
#

obj-$(CONFIG_CAN)	+= can.o
can-y			:= af_can.o proc.o

obj-$(CONFIG_CAN_RAW)	+= can-raw.o
can-raw-y		:= raw.o

obj-$(CONFIG_CAN_BCM)	+= can-bcm.o
can-bcm-y		:= bcm.o

obj-$(CONFIG_CAN_GW)	+= can-gw.o
can-gw-y		:= gw.o
