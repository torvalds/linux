# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the XFRM subsystem.
#

obj-$(CONFIG_XFRM) := xfrm_policy.o xfrm_state.o xfrm_hash.o \
		      xfrm_input.o xfrm_output.o \
		      xfrm_sysctl.o xfrm_replay.o xfrm_device.o
obj-$(CONFIG_XFRM_STATISTICS) += xfrm_proc.o
obj-$(CONFIG_XFRM_ALGO) += xfrm_algo.o
obj-$(CONFIG_XFRM_USER) += xfrm_user.o
obj-$(CONFIG_XFRM_IPCOMP) += xfrm_ipcomp.o
obj-$(CONFIG_XFRM_INTERFACE) += xfrm_interface.o
obj-$(CONFIG_INET_ESPINTCP) += espintcp.o
