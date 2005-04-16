#
# Makefile for Linux kernel Rx RPC
#

#CFLAGS += -finstrument-functions

rxrpc-objs := \
	call.o \
	connection.o \
	krxiod.o \
	krxsecd.o \
	krxtimod.o \
	main.o \
	peer.o \
	rxrpc_syms.o \
	transport.o

ifeq ($(CONFIG_PROC_FS),y)
rxrpc-objs += proc.o
endif
ifeq ($(CONFIG_SYSCTL),y)
rxrpc-objs += sysctl.o
endif

obj-$(CONFIG_RXRPC) := rxrpc.o
