# SPDX-License-Identifier: GPL-2.0
#
#  Makefile for the Linux Controller Area Network core.
#

obj-$(CONFIG_CAN)	+= can.o
can-y			:= af_can.o
can-$(CONFIG_PROC_FS)	+= proc.o

obj-$(CONFIG_CAN_RAW)	+= can-raw.o
can-raw-y		:= raw.o

obj-$(CONFIG_CAN_BCM)	+= can-bcm.o
can-bcm-y		:= bcm.o

obj-$(CONFIG_CAN_GW)	+= can-gw.o
can-gw-y		:= gw.o

obj-$(CONFIG_CAN_J1939)	+= j1939/
