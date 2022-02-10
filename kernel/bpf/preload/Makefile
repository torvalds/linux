# SPDX-License-Identifier: GPL-2.0

LIBBPF_SRCS = $(srctree)/tools/lib/bpf/
LIBBPF_INCLUDE = $(LIBBPF_SRCS)/..

obj-$(CONFIG_BPF_PRELOAD_UMD) += bpf_preload.o
CFLAGS_bpf_preload_kern.o += -I $(LIBBPF_INCLUDE)
bpf_preload-objs += bpf_preload_kern.o
