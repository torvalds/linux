#
# Makefile for Open vSwitch.
#

obj-$(CONFIG_OPENVSWITCH) += openvswitch.o

openvswitch-y := \
	actions.o \
	datapath.o \
	dp_notify.o \
	flow.o \
	flow_netlink.o \
	flow_table.o \
	vport.o \
	vport-internal_dev.o \
	vport-netdev.o

obj-$(CONFIG_OPENVSWITCH_GENEVE)+= vport-geneve.o
obj-$(CONFIG_OPENVSWITCH_GRE)	+= vport-gre.o
