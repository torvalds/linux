# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SUNRPC_XPRT_RDMA) += rpcrdma.o

rpcrdma-y := transport.o rpc_rdma.o verbs.o frwr_ops.o ib_client.o \
	svc_rdma.o svc_rdma_backchannel.o svc_rdma_transport.o \
	svc_rdma_sendto.o svc_rdma_recvfrom.o svc_rdma_rw.o \
	svc_rdma_pcl.o module.o
rpcrdma-$(CONFIG_SUNRPC_BACKCHANNEL) += backchannel.o
