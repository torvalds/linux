# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the SMACK LSM
#

obj-$(CONFIG_SECURITY_SMACK) := smack.o

smack-y := smack_lsm.o smack_access.o smackfs.o
smack-$(CONFIG_SECURITY_SMACK_NETFILTER) += smack_netfilter.o
