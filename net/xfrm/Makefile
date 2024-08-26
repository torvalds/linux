# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the XFRM subsystem.
#

xfrm_interface-$(CONFIG_XFRM_INTERFACE) += xfrm_interface_core.o

ifeq ($(CONFIG_XFRM_INTERFACE),m)
xfrm_interface-$(CONFIG_DEBUG_INFO_BTF_MODULES) += xfrm_interface_bpf.o
else ifeq ($(CONFIG_XFRM_INTERFACE),y)
xfrm_interface-$(CONFIG_DEBUG_INFO_BTF) += xfrm_interface_bpf.o
endif

obj-$(CONFIG_XFRM) := xfrm_policy.o xfrm_state.o xfrm_hash.o \
		      xfrm_input.o xfrm_output.o \
		      xfrm_sysctl.o xfrm_replay.o xfrm_device.o \
		      xfrm_nat_keepalive.o
obj-$(CONFIG_XFRM_STATISTICS) += xfrm_proc.o
obj-$(CONFIG_XFRM_ALGO) += xfrm_algo.o
obj-$(CONFIG_XFRM_USER) += xfrm_user.o
obj-$(CONFIG_XFRM_USER_COMPAT) += xfrm_compat.o
obj-$(CONFIG_XFRM_IPCOMP) += xfrm_ipcomp.o
obj-$(CONFIG_XFRM_INTERFACE) += xfrm_interface.o
obj-$(CONFIG_XFRM_ESPINTCP) += espintcp.o
obj-$(CONFIG_DEBUG_INFO_BTF) += xfrm_state_bpf.o
