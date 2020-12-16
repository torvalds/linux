# SPDX-License-Identifier: GPL-2.0

LIBBPF_SRCS = $(srctree)/tools/lib/bpf/
LIBBPF_A = $(obj)/libbpf.a
LIBBPF_OUT = $(abspath $(obj))

$(LIBBPF_A):
	$(Q)$(MAKE) -C $(LIBBPF_SRCS) OUTPUT=$(LIBBPF_OUT)/ $(LIBBPF_OUT)/libbpf.a

userccflags += -I $(srctree)/tools/include/ -I $(srctree)/tools/include/uapi \
	-I $(srctree)/tools/lib/ -Wno-unused-result

userprogs := bpf_preload_umd

clean-files := $(userprogs) bpf_helper_defs.h FEATURE-DUMP.libbpf staticobjs/ feature/

bpf_preload_umd-objs := iterators/iterators.o
bpf_preload_umd-userldlibs := $(LIBBPF_A) -lelf -lz

$(obj)/bpf_preload_umd: $(LIBBPF_A)

$(obj)/bpf_preload_umd_blob.o: $(obj)/bpf_preload_umd

obj-$(CONFIG_BPF_PRELOAD_UMD) += bpf_preload.o
bpf_preload-objs += bpf_preload_kern.o bpf_preload_umd_blob.o
