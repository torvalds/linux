# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux NET/ROM layer.
#

obj-$(CONFIG_NETROM) += netrom.o

netrom-y		:= af_netrom.o nr_dev.o nr_in.o nr_loopback.o \
			   nr_out.o nr_route.o nr_subr.o nr_timer.o
netrom-$(CONFIG_SYSCTL)	+= sysctl_net_netrom.o
