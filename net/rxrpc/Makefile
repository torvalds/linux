#
# Makefile for Linux kernel RxRPC
#

af-rxrpc-y := \
	af_rxrpc.o \
	call_accept.o \
	call_event.o \
	call_object.o \
	conn_client.o \
	conn_event.o \
	conn_object.o \
	conn_service.o \
	input.o \
	insecure.o \
	key.o \
	local_event.o \
	local_object.o \
	misc.o \
	output.o \
	peer_event.o \
	peer_object.o \
	recvmsg.o \
	security.o \
	sendmsg.o \
	skbuff.o \
	utils.o

af-rxrpc-$(CONFIG_PROC_FS) += proc.o
af-rxrpc-$(CONFIG_RXKAD) += rxkad.o
af-rxrpc-$(CONFIG_SYSCTL) += sysctl.o

obj-$(CONFIG_AF_RXRPC) += af-rxrpc.o
