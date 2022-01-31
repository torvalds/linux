# SPDX-License-Identifier: GPL-2.0

LIBBPF_SRCS = $(srctree)/tools/lib/bpf/
LIBBPF_INCLUDE = $(LIBBPF_SRCS)/..

userccflags += -I $(srctree)/tools/include/ -I $(srctree)/tools/include/uapi \
	-I $(LIBBPF_INCLUDE) -Wno-unused-result

userprogs := bpf_preload_umd

bpf_preload_umd-objs := iterators/iterators.o

$(obj)/bpf_preload_umd:

$(obj)/bpf_preload_umd_blob.o: $(obj)/bpf_preload_umd

obj-$(CONFIG_BPF_PRELOAD_UMD) += bpf_preload.o
bpf_preload-objs += bpf_preload_kern.o bpf_preload_umd_blob.o
