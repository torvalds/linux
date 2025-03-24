# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux AppleTalk layer.
#

obj-$(CONFIG_ATALK) += appletalk.o

appletalk-y			:= aarp.o ddp.o
appletalk-$(CONFIG_PROC_FS)	+= atalk_proc.o
appletalk-$(CONFIG_SYSCTL)	+= sysctl_net_atalk.o
