#
# Makefile for the Linux NFC HCI layer.
#

obj-$(CONFIG_NFC_HCI) += hci.o

hci-y			:= core.o hcp.o command.o
hci-$(CONFIG_NFC_SHDLC)	+= shdlc.o
