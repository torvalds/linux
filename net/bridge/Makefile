# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the IEEE 802.1d ethernet bridging layer.
#

obj-$(CONFIG_BRIDGE) += bridge.o

bridge-y	:= br.o br_device.o br_fdb.o br_forward.o br_if.o br_input.o \
			br_ioctl.o br_stp.o br_stp_bpdu.o \
			br_stp_if.o br_stp_timer.o br_netlink.o \
			br_netlink_tunnel.o br_arp_nd_proxy.o

bridge-$(CONFIG_SYSFS) += br_sysfs_if.o br_sysfs_br.o

bridge-$(subst m,y,$(CONFIG_BRIDGE_NETFILTER)) += br_nf_core.o

br_netfilter-y := br_netfilter_hooks.o
br_netfilter-$(subst m,y,$(CONFIG_IPV6)) += br_netfilter_ipv6.o
obj-$(CONFIG_BRIDGE_NETFILTER) += br_netfilter.o

bridge-$(CONFIG_BRIDGE_IGMP_SNOOPING) += br_multicast.o br_mdb.o br_multicast_eht.o

bridge-$(CONFIG_BRIDGE_VLAN_FILTERING) += br_vlan.o br_vlan_tunnel.o br_vlan_options.o

bridge-$(CONFIG_NET_SWITCHDEV) += br_switchdev.o

obj-$(CONFIG_NETFILTER) += netfilter/

bridge-$(CONFIG_BRIDGE_MRP)	+= br_mrp_switchdev.o br_mrp.o br_mrp_netlink.o

bridge-$(CONFIG_BRIDGE_CFM)	+= br_cfm.o br_cfm_netlink.o
