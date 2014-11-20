#
# Makefile for the linux lock manager stuff
#

obj-$(CONFIG_LOCKD) += lockd.o

lockd-objs-y := clntlock.o clntproc.o clntxdr.o host.o svc.o svclock.o \
	        svcshare.o svcproc.o svcsubs.o mon.o xdr.o
lockd-objs-$(CONFIG_LOCKD_V4) += clnt4xdr.o xdr4.o svc4proc.o
lockd-objs-$(CONFIG_PROC_FS) += procfs.o
lockd-objs		      := $(lockd-objs-y)
