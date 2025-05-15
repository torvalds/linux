# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for HSR
#

obj-$(CONFIG_HSR)	+= hsr.o

hsr-y			:= hsr_main.o hsr_framereg.o hsr_device.o \
			   hsr_netlink.o hsr_slave.o hsr_forward.o
hsr-$(CONFIG_DEBUG_FS) += hsr_debugfs.o

obj-$(CONFIG_PRP_DUP_DISCARD_KUNIT_TEST) += prp_dup_discard_test.o
