#
# Makefile for Open vSwitch.
#

obj-$(CONFIG_OPENVSWITCH) += openvswitch.o

openvswitch-y := \
	actions.o \
	datapath.o \
	dp_notify.o \
	flow.o \
	vport.o \
	vport-internal_dev.o \
	vport-netdev.o

ifneq ($(CONFIG_OPENVSWITCH_VXLAN),)
openvswitch-y += vport-vxlan.o
endif

ifneq ($(CONFIG_OPENVSWITCH_GRE),)
openvswitch-y += vport-gre.o
endif
