#
# Makefile for Linux kernel RxRPC
#

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
