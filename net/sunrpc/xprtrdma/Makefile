obj-$(CONFIG_SUNRPC_XPRT_RDMA) += xprtrdma.o

xprtrdma-y := transport.o rpc_rdma.o verbs.o

obj-$(CONFIG_SUNRPC_XPRT_RDMA) += svcrdma.o

svcrdma-y := svc_rdma.o svc_rdma_transport.o \
	svc_rdma_marshal.o svc_rdma_sendto.o svc_rdma_recvfrom.o
