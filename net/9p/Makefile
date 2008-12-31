obj-$(CONFIG_NET_9P) := 9pnet.o
obj-$(CONFIG_NET_9P_VIRTIO) += 9pnet_virtio.o
obj-$(CONFIG_NET_9P_RDMA) += 9pnet_rdma.o

9pnet-objs := \
	mod.o \
	client.o \
	error.o \
	util.o \
	protocol.o \
	trans_fd.o \

9pnet_virtio-objs := \
	trans_virtio.o \

9pnet_rdma-objs := \
	trans_rdma.o \
