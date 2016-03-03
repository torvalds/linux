#
# Makefile for the Linux Bluetooth subsystem.
#

obj-$(CONFIG_BT)	+= bluetooth.o
obj-$(CONFIG_BT_RFCOMM)	+= rfcomm/
obj-$(CONFIG_BT_BNEP)	+= bnep/
obj-$(CONFIG_BT_CMTP)	+= cmtp/
obj-$(CONFIG_BT_HIDP)	+= hidp/
obj-$(CONFIG_BT_6LOWPAN) += bluetooth_6lowpan.o

bluetooth_6lowpan-y := 6lowpan.o

bluetooth-y := af_bluetooth.o hci_core.o hci_conn.o hci_event.o mgmt.o \
	hci_sock.o hci_sysfs.o l2cap_core.o l2cap_sock.o smp.o lib.o \
	ecc.o hci_request.o mgmt_util.o

bluetooth-$(CONFIG_BT_BREDR) += sco.o
bluetooth-$(CONFIG_BT_HS) += a2mp.o amp.o
bluetooth-$(CONFIG_BT_LEDS) += leds.o
bluetooth-$(CONFIG_BT_DEBUGFS) += hci_debugfs.o
bluetooth-$(CONFIG_BT_SELFTEST) += selftest.o

subdir-ccflags-y += -D__CHECK_ENDIAN__
