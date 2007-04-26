#
# Makefile for Linux kernel Rx RPC
#

#CFLAGS += -finstrument-functions

af-rxrpc-objs := \
	af_rxrpc.o \
	ar-accept.o \
	ar-ack.o \
	ar-call.o \
	ar-connection.o \
	ar-connevent.o \
	ar-error.o \
	ar-input.o \
	ar-key.o \
	ar-local.o \
	ar-output.o \
	ar-peer.o \
	ar-recvmsg.o \
	ar-security.o \
	ar-skbuff.o \
	ar-transport.o

ifeq ($(CONFIG_PROC_FS),y)
af-rxrpc-objs += ar-proc.o
endif

obj-$(CONFIG_AF_RXRPC) += af-rxrpc.o

obj-$(CONFIG_RXKAD) += rxkad.o

#
# obsolete RxRPC interface, still used by fs/afs/
#
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

obj-$(CONFIG_RXRPC) += rxrpc.o
