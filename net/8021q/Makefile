#
# Makefile for the Linux VLAN layer.
#

obj-$(CONFIG_VLAN_8021Q) += 8021q.o

8021q-objs := vlan.o vlan_dev.o vlan_netlink.o

ifeq ($(CONFIG_PROC_FS),y)
8021q-objs += vlanproc.o
endif

