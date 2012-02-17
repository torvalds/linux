#
# Makefile for the Linux NFC subsystem.
#

obj-$(CONFIG_NFC) += nfc.o
obj-$(CONFIG_NFC_NCI) += nci/

nfc-objs := core.o netlink.o af_nfc.o rawsock.o
nfc-$(CONFIG_NFC_LLCP)	+= llcp/llcp.o llcp/commands.o llcp/sock.o
