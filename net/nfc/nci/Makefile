#
# Makefile for the Linux NFC NCI layer.
#

obj-$(CONFIG_NFC_NCI) += nci.o

nci-objs := core.o data.o lib.o ntf.o rsp.o hci.o

nci-$(CONFIG_NFC_NCI_SPI) += spi.o

nci_uart-y += uart.o
obj-$(CONFIG_NFC_NCI_UART) += nci_uart.o
