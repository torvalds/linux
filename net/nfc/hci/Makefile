# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux NFC HCI layer.
#

obj-$(CONFIG_NFC_HCI) += hci.o

hci-y			:= core.o hcp.o command.o llc.o llc_nop.o
hci-$(CONFIG_NFC_SHDLC) += llc_shdlc.o
