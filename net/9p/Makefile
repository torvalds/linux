# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_NET_9P) := 9pnet.o
obj-$(CONFIG_NET_9P_FD) += 9pnet_fd.o
obj-$(CONFIG_NET_9P_XEN) += 9pnet_xen.o
obj-$(CONFIG_NET_9P_VIRTIO) += 9pnet_virtio.o
obj-$(CONFIG_NET_9P_RDMA) += 9pnet_rdma.o

9pnet-objs := \
	mod.o \
	client.o \
	error.o \
	protocol.o \
	trans_common.o \

9pnet_fd-objs := \
	trans_fd.o \

9pnet_virtio-objs := \
	trans_virtio.o \

9pnet_xen-objs := \
	trans_xen.o \

9pnet_rdma-objs := \
	trans_rdma.o \
