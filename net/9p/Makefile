obj-$(CONFIG_NET_9P) := 9pnet.o
obj-$(CONFIG_NET_9P_FD) += 9pnet_fd.o
obj-$(CONFIG_NET_9P_VIRTIO) += 9pnet_virtio.o

9pnet-objs := \
	mod.o \
	mux.o \
	client.o \
	conv.o \
	error.o \
	fcprint.o \
	util.o \

9pnet_fd-objs := \
	trans_fd.o \

9pnet_virtio-objs := \
	trans_virtio.o \
