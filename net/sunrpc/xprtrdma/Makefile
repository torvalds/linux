obj-$(CONFIG_SUNRPC_XPRT_RDMA) += rpcrdma.o

rpcrdma-y := transport.o rpc_rdma.o verbs.o \
	fmr_ops.o frwr_ops.o \
	svc_rdma.o svc_rdma_backchannel.o svc_rdma_transport.o \
	svc_rdma_sendto.o svc_rdma_recvfrom.o svc_rdma_rw.o \
	module.o
rpcrdma-$(CONFIG_SUNRPC_BACKCHANNEL) += backchannel.o
