# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_DECNET) += decnet.o

decnet-y := af_decnet.o dn_nsp_in.o dn_nsp_out.o \
	    dn_route.o dn_dev.o dn_neigh.o dn_timer.o
decnet-$(CONFIG_DECNET_ROUTER) += dn_fib.o dn_rules.o dn_table.o
decnet-y += sysctl_net_decnet.o

obj-$(CONFIG_NETFILTER) += netfilter/

