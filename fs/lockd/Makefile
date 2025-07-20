# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux lock manager stuff
#

ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_LOCKD) += lockd.o

lockd-y := clntlock.o clntproc.o clntxdr.o host.o svc.o svclock.o \
	   svcshare.o svcproc.o svcsubs.o mon.o trace.o xdr.o netlink.o
lockd-$(CONFIG_LOCKD_V4) += clnt4xdr.o xdr4.o svc4proc.o
lockd-$(CONFIG_PROC_FS) += procfs.o
