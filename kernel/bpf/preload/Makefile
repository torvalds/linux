# SPDX-License-Identifier: GPL-2.0

LIBBPF_SRCS = $(srctree)/tools/lib/bpf/
LIBBPF_OUT = $(abspath $(obj))/libbpf
LIBBPF_A = $(LIBBPF_OUT)/libbpf.a
LIBBPF_DESTDIR = $(LIBBPF_OUT)
LIBBPF_INCLUDE = $(LIBBPF_DESTDIR)/include

# Although not in use by libbpf's Makefile, set $(O) so that the "dummy" test
# in tools/scripts/Makefile.include always succeeds when building the kernel
# with $(O) pointing to a relative path, as in "make O=build bindeb-pkg".
$(LIBBPF_A): | $(LIBBPF_OUT)
	$(Q)$(MAKE) -C $(LIBBPF_SRCS) O=$(LIBBPF_OUT)/ OUTPUT=$(LIBBPF_OUT)/   \
		DESTDIR=$(LIBBPF_DESTDIR) prefix=			       \
		$(LIBBPF_OUT)/libbpf.a install_headers

libbpf_hdrs: $(LIBBPF_A)

.PHONY: libbpf_hdrs

$(LIBBPF_OUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $@

userccflags += -I $(srctree)/tools/include/ -I $(srctree)/tools/include/uapi \
	-I $(LIBBPF_INCLUDE) -Wno-unused-result

userprogs := bpf_preload_umd

clean-files := libbpf/

$(obj)/iterators/iterators.o: | libbpf_hdrs

bpf_preload_umd-objs := iterators/iterators.o
bpf_preload_umd-userldlibs := $(LIBBPF_A) -lelf -lz

$(obj)/bpf_preload_umd: $(LIBBPF_A)

$(obj)/bpf_preload_umd_blob.o: $(obj)/bpf_preload_umd

obj-$(CONFIG_BPF_PRELOAD_UMD) += bpf_preload.o
bpf_preload-objs += bpf_preload_kern.o bpf_preload_umd_blob.o
