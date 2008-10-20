#
# Makefile for the linux lock manager stuff
#

obj-$(CONFIG_LOCKD) += lockd.o

lockd-objs-y := clntlock.o clntproc.o host.o svc.o svclock.o svcshare.o \
	        svcproc.o svcsubs.o mon.o xdr.o grace.o
lockd-objs-$(CONFIG_LOCKD_V4) += xdr4.o svc4proc.o
lockd-objs		      := $(lockd-objs-y)
