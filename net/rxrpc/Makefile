# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Linux kernel RxRPC
#

obj-$(CONFIG_AF_RXRPC) += rxrpc.o

rxrpc-y := \
	af_rxrpc.o \
	call_accept.o \
	call_event.o \
	call_object.o \
	call_state.o \
	conn_client.o \
	conn_event.o \
	conn_object.o \
	conn_service.o \
	input.o \
	insecure.o \
	io_thread.o \
	key.o \
	local_event.o \
	local_object.o \
	misc.o \
	net_ns.o \
	output.o \
	peer_event.o \
	peer_object.o \
	recvmsg.o \
	rtt.o \
	security.o \
	sendmsg.o \
	server_key.o \
	skbuff.o \
	txbuf.o \
	utils.o

rxrpc-$(CONFIG_PROC_FS) += proc.o
rxrpc-$(CONFIG_RXKAD) += rxkad.o
rxrpc-$(CONFIG_SYSCTL) += sysctl.o


obj-$(CONFIG_RXPERF) += rxperf.o
