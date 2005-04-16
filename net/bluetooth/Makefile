#
# Makefile for the Linux Bluetooth subsystem.
#

obj-$(CONFIG_BT)	+= bluetooth.o
obj-$(CONFIG_BT_L2CAP)	+= l2cap.o
obj-$(CONFIG_BT_SCO)	+= sco.o
obj-$(CONFIG_BT_RFCOMM)	+= rfcomm/
obj-$(CONFIG_BT_BNEP)	+= bnep/
obj-$(CONFIG_BT_CMTP)	+= cmtp/
obj-$(CONFIG_BT_HIDP)	+= hidp/

bluetooth-objs := af_bluetooth.o hci_core.o hci_conn.o hci_event.o hci_sock.o hci_sysfs.o lib.o
