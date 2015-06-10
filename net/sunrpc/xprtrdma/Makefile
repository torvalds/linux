obj-$(CONFIG_SUNRPC_XPRT_RDMA_CLIENT) += xprtrdma.o

xprtrdma-y := transport.o rpc_rdma.o verbs.o \
	fmr_ops.o frwr_ops.o physical_ops.o

obj-$(CONFIG_SUNRPC_XPRT_RDMA_SERVER) += svcrdma.o

svcrdma-y := svc_rdma.o svc_rdma_transport.o \
	svc_rdma_marshal.o svc_rdma_sendto.o svc_rdma_recvfrom.o
