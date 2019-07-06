# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux Bluetooth RFCOMM layer.
#

obj-$(CONFIG_BT_RFCOMM) += rfcomm.o

rfcomm-y			:= core.o sock.o
rfcomm-$(CONFIG_BT_RFCOMM_TTY)	+= tty.o
