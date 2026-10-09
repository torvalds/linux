# SPDX-License-Identifier: GPL-2.0
include ../../../build/Build.include
include ../../../scripts/Makefile.arch
include ../../../scripts/Makefile.include

BPF_GCC		?= $(shell command -v bpf-gcc;)

SKIP_DOCS	?=
SKIP_LLVM	?=
SKIP_LIBBFD	?=
SKIP_CRYPTO	?=

# Check whether bpf cpu=v4 is supported or not by clang
ifneq ($(shell $(CLANG) --target=bpf -mcpu=help 2>&1 | grep 'v4'),)
CLANG_CPUV4 := 1
endif

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_progs \
	test_sockmap \
	test_tcpnotify_user \
	test_progs-no_alu32
TEST_INST_SUBDIRS := no_alu32

# Also test bpf-gcc, if present
ifneq ($(BPF_GCC),)
TEST_GEN_PROGS += test_progs-bpf_gcc
TEST_INST_SUBDIRS += bpf_gcc
endif

ifneq ($(CLANG_CPUV4),)
TEST_GEN_PROGS += test_progs-cpuv4
TEST_INST_SUBDIRS += cpuv4
endif

TEST_FILES = xsk_prereqs.sh $(wildcard progs/btf_dump_test_case_*.c)	\
	     bpftool_btf_dump_sorted.expected					\
	     bpftool_btf_dump_unsorted.expected

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_bpftool_build.sh \
	test_doc_build.sh \
	test_xsk.sh \
	test_xdp_features.sh

TEST_PROGS_EXTENDED := \
	ima_setup.sh verify_sig_setup.sh

TEST_KMODS := bpf_testmod.ko bpf_test_no_cfi.ko bpf_test_modorder_x.ko \
	bpf_test_modorder_y.ko bpf_test_rqspinlock.ko
TEST_KMOD_TARGETS = $(addprefix $(OUTPUT)/,$(TEST_KMODS))

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = \
	bench \
	flow_dissector_load \
	test_cpp \
	veristat \
	xdp_features \
	xdp_hw_metadata \
	xdp_synproxy \
	xskxceiver

TEST_GEN_FILES += $(TEST_KMODS) liburandom_read.so urandom_read sign-file uprobe_multi

ifneq ($(V),1)
submake_extras := feature_display=0
endif

# override lib.mk's default rules
OVERRIDE_TARGETS := 1
override define CLEAN
	$(call msg,CLEAN)
	$(Q)$(RM) -r $(TEST_GEN_PROGS)
	$(Q)$(RM) -r $(TEST_GEN_PROGS_EXTENDED)
	$(Q)$(RM) -r $(TEST_GEN_FILES)
	$(Q)$(RM) -r $(TEST_KMODS)
	$(Q)$(RM) -r $(EXTRA_CLEAN)
	$(Q)$(MAKE) -C test_kmods clean
	$(Q)$(MAKE) -C libarena clean
	$(Q)$(MAKE) docs-clean
endef

# Append build flags before lib.mk's user flags. References to
# Makefile.buildvars definitions expand when the flags are used.
# The arena-ASAN define is appended after lib.mk.
CFLAGS += $(COMMON_CFLAGS) $(PCAP_CFLAGS) $(CLANG_WARN_CFLAGS)
LDFLAGS += $(SAN_LDFLAGS)
include ../lib.mk
include Makefile.buildvars

BPFTOOLDIR := $(TOOLSDIR)/bpf/bpftool
HOST_BPFOBJ := $(HOST_BUILD_DIR)/libbpf/libbpf.a
BPF_TARGET_ENDIAN:=$(if $(IS_LITTLE_ENDIAN),--target=bpfel,--target=bpfeb)

NON_CHECK_FEAT_TARGETS := clean docs-clean emit_tests
CHECK_FEAT := $(filter-out $(NON_CHECK_FEAT_TARGETS),$(or $(MAKECMDGOALS), "none"))
ifneq ($(CHECK_FEAT),)
FEATURE_USER := .selftests
FEATURE_TESTS := llvm
FEATURE_DISPLAY := $(FEATURE_TESTS)

# Makefile.feature expects OUTPUT to end with a slash
ifeq ($(shell expr $(MAKE_VERSION) \>= 4.4), 1)
$(let OUTPUT,$(OUTPUT)/,\
	$(eval include ../../../build/Makefile.feature))
else
override OUTPUT := $(OUTPUT)/
$(eval include ../../../build/Makefile.feature)
override OUTPUT := $(patsubst %/,%,$(OUTPUT))
endif
endif

ifneq ($(SKIP_LLVM),1)
ifeq ($(feature-llvm),1)
  LLVM_CFLAGS  += -DHAVE_LLVM_SUPPORT
  LLVM_CONFIG_LIB_COMPONENTS := mcdisassembler all-targets
  # both llvm-config and lib.mk add -D_GNU_SOURCE, which ends up as conflict
  LLVM_CFLAGS  += $(filter-out -D_GNU_SOURCE,$(shell $(LLVM_CONFIG) --cflags))
  # Cross compilation must use dynamic linking to avoid unresolved library
  # dependencies. For native build, prefer linking statically if it's
  # available, otherwise fallback to shared.
  ifneq ($(ARCH), $(HOSTARCH))
    LLVM_LINK_STATIC :=
  else
    LLVM_LINK_STATIC := $(shell $(LLVM_CONFIG) --link-static --libs >/dev/null 2>&1 && echo y)
  endif
  ifeq ($(LLVM_LINK_STATIC),y)
    LLVM_LDLIBS  += $(shell $(LLVM_CONFIG) --link-static --libs $(LLVM_CONFIG_LIB_COMPONENTS))
    LLVM_LDLIBS  += $(filter-out -lxml2,$(shell $(LLVM_CONFIG) --link-static --system-libs $(LLVM_CONFIG_LIB_COMPONENTS)))
    LLVM_LDLIBS  += -lstdc++
  else
    LLVM_LDLIBS  += $(shell $(LLVM_CONFIG) --link-shared --libs $(LLVM_CONFIG_LIB_COMPONENTS))
  endif
  LLVM_LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags)
endif
endif

# Some utility functions use LLVM libraries
$(OUTPUT)/jit_disasm_helpers.o: CFLAGS += $(LLVM_CFLAGS)

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))
ifeq ($(VMLINUX_BTF),)
$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)")
endif

# Define simple and short `make test_progs`, `make test_maps`, etc targets
# to build individual tests.
# NOTE: Semicolon at the end is critical to override lib.mk's default static
# rule for binaries.
$(notdir $(TEST_GEN_PROGS) $(TEST_KMODS)				\
	 $(TEST_GEN_PROGS_EXTENDED)): %: $(OUTPUT)/% ;

# sort removes libbpf duplicates when not cross-building
MAKE_DIRS := $(sort $(OUTPUT) $(BUILD_DIR)/libbpf $(HOST_BUILD_DIR)/libbpf \
	       $(BUILD_DIR)/bpftool $(HOST_BUILD_DIR)/bpftool		\
	       $(HOST_BUILD_DIR)/resolve_btfids				\
	       $(INCLUDE_DIR))
$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

$(OUTPUT)/%.o: %.c
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $(filter %.c,$^) $(LDLIBS) -o $@

# Keep header prerequisites off the link command: clang treats
# bare headers as precompiled-header jobs and refuses -o.
$(OUTPUT)/%:%.c
	$(call msg,BINARY,,$@)
	$(Q)$(LINK.c) $(filter-out %.h,$^) $(LDLIBS) -o $@

# LLVM's ld.lld doesn't support all the architectures, so use it only on x86
ifeq ($(SRCARCH),$(filter $(SRCARCH),x86 riscv))
LLD := lld
else
LLD := $(shell command -v $(LD))
endif

# Filter out -static for liburandom_read.so and its dependent targets so that static builds
# do not fail. Static builds leave urandom_read relying on system-wide shared libraries.
$(OUTPUT)/liburandom_read.so: urandom_read_lib1.c urandom_read_lib2.c liburandom_read.map
	$(call msg,LIB,,$@)
	$(Q)$(CLANG) $(CLANG_TARGET_ARCH) \
		     $(filter-out -static,$(COMMON_CFLAGS) $(LDFLAGS)) \
		     $(filter %.c,$^) $(filter-out -static,$(LDLIBS)) \
		     -Wno-unused-command-line-argument \
		     -fuse-ld=$(LLD) -Wl,-znoseparate-code -Wl,--build-id=sha1 \
		     -Wl,--version-script=liburandom_read.map \
		     -fPIC -shared -o $@

$(OUTPUT)/urandom_read: urandom_read.c urandom_read_aux.c $(OUTPUT)/liburandom_read.so
	$(call msg,BINARY,,$@)
	$(Q)$(CLANG) $(CLANG_TARGET_ARCH) \
		     $(filter-out -static,$(COMMON_CFLAGS) $(LDFLAGS)) $(filter %.c,$^) \
		     -Wno-unused-command-line-argument \
		     -lurandom_read $(filter-out -static,$(LDLIBS)) -L$(OUTPUT) \
		     -fuse-ld=$(LLD) -Wl,-znoseparate-code -Wl,--build-id=sha1 \
		     -Wl,-rpath=. -o $@

$(OUTPUT)/sign-file: ../../../../scripts/sign-file.c
	$(call msg,SIGN-FILE,,$@)
	$(Q)$(CC) $(shell $(PKG_CONFIG) --cflags libcrypto 2> /dev/null) \
		  -I$(srctree)/tools/include/uapi/ \
		  $< -o $@ \
		  $(shell $(PKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)

# This should really be a grouped target, but make versions before 4.3 don't
# support that for regular rules. However, pattern matching rules are implicitly
# treated as grouped even with older versions of make, so as a workaround, the
# subst() turns the rule into a pattern matching rule
$(addprefix test_kmods/,$(subst .ko,%ko,$(TEST_KMODS))): $(VMLINUX_BTF) $(RESOLVE_BTFIDS) $(wildcard test_kmods/Makefile test_kmods/*.[ch])
	$(Q)$(RM) test_kmods/*.ko test_kmods/*.mod.o # force re-compilation
	$(Q)$(MAKE) $(submake_extras) -C test_kmods				\
		$(if $(O),O=$(abspath $(O)))					\
		$(if $(KBUILD_OUTPUT),KBUILD_OUTPUT=$(abspath $(KBUILD_OUTPUT)))\
		RESOLVE_BTFIDS=$(RESOLVE_BTFIDS)				\
		EXTRA_CFLAGS='' EXTRA_LDFLAGS=''

$(TEST_KMOD_TARGETS): $(addprefix test_kmods/,$(TEST_KMODS))
	$(call msg,MOD,,$@)
	$(Q)$(if $(PERMISSIVE),if [ -f test_kmods/$(@F) ]; then )cp test_kmods/$(@F) $@$(if $(PERMISSIVE),; fi)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(BPFOBJ)

TESTING_HELPERS	:= $(OUTPUT)/testing_helpers.o
CGROUP_HELPERS	:= $(OUTPUT)/cgroup_helpers.o
UNPRIV_HELPERS  := $(OUTPUT)/unpriv_helpers.o
TRACE_HELPERS	:= $(OUTPUT)/trace_helpers.o
JSON_WRITER		:= $(OUTPUT)/json_writer.o
CAP_HELPERS	:= $(OUTPUT)/cap_helpers.o
NETWORK_HELPERS := $(OUTPUT)/network_helpers.o

$(OUTPUT)/test_sockmap: $(CGROUP_HELPERS) $(TESTING_HELPERS)
$(OUTPUT)/test_tcpnotify_user: $(CGROUP_HELPERS) $(TESTING_HELPERS) $(TRACE_HELPERS)
$(OUTPUT)/test_sock_fields: $(CGROUP_HELPERS) $(TESTING_HELPERS)
$(OUTPUT)/test_tag: $(TESTING_HELPERS)
$(OUTPUT)/flow_dissector_load: $(TESTING_HELPERS) flow_dissector_load.h
$(OUTPUT)/test_verifier: $(TESTING_HELPERS) $(CAP_HELPERS) $(UNPRIV_HELPERS)

# Defined ahead of the first rule that lists it as a prerequisite.
VERIFY_SIG_HDR := verification_cert.h

# The userspace objects the runner instances link (RUNNER_OBJS, see
# Makefile.buildvars) are built here, once, so that no runner sub-make
# ever writes a file this Makefile also builds.
#
# Helper C files include headers installed to $(INCLUDE_DIR) by the
# $(TRUNNER_BPFTOOL) and $(BPFOBJ) targets.
RUNNER_OBJS := $(sort $(RUNNER_OBJS-test_progs) $(RUNNER_OBJS-test_maps))
$(RUNNER_OBJS): $(VERIFY_SIG_HDR) $(BPFOBJ) $(TRUNNER_BPFTOOL)
$(OUTPUT)/test_progs.o: prog_tests/tests.h
$(OUTPUT)/test_maps.o: map_tests/tests.h

# find_bit.c is the only runner source outside this directory, so it
# needs a rule of its own.
$(OUTPUT)/find_bit.o: $(TOOLSDIR)/lib/find_bit.c
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $< $(LDLIBS) -o $@

$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)    \
		    $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)			       \
		    ARCH= CROSS_COMPILE= CC="$(HOSTCC)" LD="$(HOSTLD)" 	       \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(SAN_CFLAGS) $(EXTRA_CFLAGS)'	       \
		    EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)'			       \
		    OUTPUT=$(HOST_BUILD_DIR)/bpftool/			       \
		    LIBBPF_OUTPUT=$(HOST_BUILD_DIR)/libbpf/		       \
		    LIBBPF_DESTDIR=$(HOST_SCRATCH_DIR)/			       \
		    SKIP_LLVM=$(SKIP_LLVM)				       \
		    SKIP_LIBBFD=$(SKIP_LIBBFD)				       \
		    SKIP_CRYPTO=$(SKIP_CRYPTO)				       \
		    prefix= DESTDIR=$(HOST_SCRATCH_DIR)/ install-bin

ifneq ($(CROSS_COMPILE),)
$(CROSS_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)	\
		    $(BPFOBJ) | $(BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)				\
		    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)			\
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(SAN_CFLAGS) $(EXTRA_CFLAGS)'	       \
		    EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)'			       \
		    OUTPUT=$(BUILD_DIR)/bpftool/				\
		    LIBBPF_OUTPUT=$(BUILD_DIR)/libbpf/				\
		    LIBBPF_DESTDIR=$(SCRATCH_DIR)/				\
		    SKIP_LLVM=$(SKIP_LLVM)					\
		    SKIP_LIBBFD=$(SKIP_LIBBFD)					\
		    SKIP_CRYPTO=$(SKIP_CRYPTO)					\
		    prefix= DESTDIR=$(SCRATCH_DIR)/ install-bin
endif

ifneq ($(SKIP_DOCS),1)
all: docs
endif

docs:
	$(Q)RST2MAN_OPTS="--exit-status=1" $(MAKE) $(submake_extras)	\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

docs-clean:
	$(Q)$(MAKE) $(submake_extras)					\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   $(APIDIR)/linux/bpf.h					       \
	   | $(BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(SAN_CFLAGS) $(EXTRA_CFLAGS)' \
		    EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)'	       \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

ifneq ($(BPFOBJ),$(HOST_BPFOBJ))
$(HOST_BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
		$(APIDIR)/linux/bpf.h					       \
		| $(HOST_BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR)                             \
		    ARCH= CROSS_COMPILE=				       \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(EXTRA_CFLAGS)'	       \
		    EXTRA_LDFLAGS='$(EXTRA_LDFLAGS)'			       \
		    OUTPUT=$(HOST_BUILD_DIR)/libbpf/			       \
		    CC="$(HOSTCC)" LD="$(HOSTLD)"			       \
		    DESTDIR=$(HOST_SCRATCH_DIR)/ prefix= all install_headers
endif

# vmlinux.h is first dumped to a temporary file and then compared to
# the previous version. This helps to avoid unnecessary re-builds of
# BPF objects.
$(INCLUDE_DIR)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL) | $(INCLUDE_DIR)
ifeq ($(VMLINUX_H),)
	$(call msg,GEN,,$@)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $(INCLUDE_DIR)/.vmlinux.h.tmp
	$(Q)cmp -s $(INCLUDE_DIR)/.vmlinux.h.tmp $@ || mv $(INCLUDE_DIR)/.vmlinux.h.tmp $@
else
	$(call msg,CP,,$@)
	$(Q)cp "$(VMLINUX_H)" $@
endif

$(RESOLVE_BTFIDS): $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/resolve_btfids	\
		       $(TOOLSDIR)/bpf/resolve_btfids/main.c		\
		       $(TOOLSDIR)/bpf/resolve_btfids/btf_colors.c	\
		       $(TOOLSDIR)/bpf/resolve_btfids/btf_colors.h	\
		       $(TOOLSDIR)/lib/rbtree.c			\
		       $(TOOLSDIR)/lib/zalloc.c			\
		       $(TOOLSDIR)/lib/string.c			\
		       $(TOOLSDIR)/lib/ctype.c			\
		       $(TOOLSDIR)/lib/str_error_r.c
	$(Q)$(MAKE) $(submake_extras) -C $(TOOLSDIR)/bpf/resolve_btfids	\
		CC="$(HOSTCC)" LD="$(HOSTLD)" AR="$(HOSTAR)" \
		LIBBPF_INCLUDE=$(HOST_INCLUDE_DIR) \
		EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)' \
		HOSTPKG_CONFIG='$(PKG_CONFIG)' \
		OUTPUT=$(HOST_BUILD_DIR)/resolve_btfids/ BPFOBJ=$(HOST_BPFOBJ)

# Get Clang's default includes on this system, as opposed to those seen by
# '--target=bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__loongarch_grlen ' | awk '{printf("-D__BITS_PER_LONG=%d", $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep -E 'MIPS(EL|EB)|_MIPS_SZ(PTR|LONG) |_MIPS_SIM |_ABI(O32|N32|64) ' | awk '{printf("-D%s=%s ", $$2, $$3)}')
endef

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))
CLANG_CFLAGS = $(CLANG_SYS_INCLUDES)

# genkey writes both outputs. Pattern rules group them even on make
# versions before 4.3, preventing concurrent genkey invocations.
# Restrict the pattern to *_key files because genkey uses fixed names.
$(BUILD_DIR)/%_key.pem $(BUILD_DIR)/%_key.der: $(VERIFY_SIG_SETUP)
	$(Q)mkdir -p $(BUILD_DIR)
	$(Q)$(VERIFY_SIG_SETUP) genkey $(BUILD_DIR)

# Generates a header with C array declaration, containing test_progs_verification_cert bytes
$(VERIFY_SIG_HDR): $(VERIFICATION_CERT)
	$(Q)(echo "unsigned char test_progs_verification_cert[] = {"; \
	 od -v -t 'xC' -w12 $< | sed 's/ \(\S\+\)/ 0x\1,/g;s/^\S\+/ /;$$d'; \
	 echo "};"; \
	 echo "unsigned int test_progs_verification_cert_len = $$(wc -c < $<);") > $@

LIBARENA_MAKE_ARGS = \
		BPFTOOL="$(BPFTOOL)" \
		INCLUDE_DIR="$(INCLUDE_DIR)" \
		LIBBPF_INCLUDE="$(HOST_INCLUDE_DIR)" \
		BPFOBJ="$(BPFOBJ)" \
		LDLIBS="$(LDLIBS) -lzstd" \
		CLANG="$(CLANG)" \
		BPF_CFLAGS="$(BPF_CFLAGS) $(CLANG_CFLAGS)" \
		BPF_TARGET_ENDIAN="$(BPF_TARGET_ENDIAN)" \
		Q="$(Q)"

LIBARENA_COMMON_DEPS := $(wildcard libarena/Makefile		\
				    libarena/include/*		\
				    libarena/include/libarena/*	\
				    libarena/src/*)
LIBARENA_BPF_DEPS := $(LIBARENA_COMMON_DEPS) $(wildcard libarena/selftests/*)
LIBARENA_BENCH_BPF_DEPS := $(LIBARENA_COMMON_DEPS) $(wildcard libarena/benchs/*)

LIBARENA_BENCH_SKEL := libarena/libarena_bench.skel.h

$(LIBARENA_SKEL): $(INCLUDE_DIR)/vmlinux.h $(BPFOBJ) $(LIBARENA_BPF_DEPS)
	+$(MAKE) -C libarena libarena.skel.h $(LIBARENA_MAKE_ARGS)

$(LIBARENA_BENCH_SKEL): $(INCLUDE_DIR)/vmlinux.h $(BPFOBJ) $(LIBARENA_BENCH_BPF_DEPS) | $(LIBARENA_SKEL)
	+$(MAKE) -C libarena benchmarks $(LIBARENA_MAKE_ARGS)

ifneq ($(CLANG_HAS_ARENA_ASAN),)
$(LIBARENA_ASAN_SKEL): $(INCLUDE_DIR)/vmlinux.h $(BPFOBJ) $(LIBARENA_BPF_DEPS)
	+$(MAKE) -C libarena libarena_asan.skel.h $(LIBARENA_MAKE_ARGS)
endif

# #![no_std] looks for compiler_builtins too. Nothing of it is used.
ifneq ($(RUST_CORE),)
$(RUST_CORE): $(RUST_CORE_SRC)
	$(call msg,RUSTC,,$@)
	$(Q)mkdir -p $(@D)
	+$(Q)$(RUSTC_BPF) -A warnings --edition 2024 --crate-name core --out-dir $(@D) $<
	+$(Q)echo '#![feature(compiler_builtins)] #![compiler_builtins] #![no_std]' | \
		$(RUSTC_BPF) -A warnings --crate-name compiler_builtins --out-dir $(@D) -
endif

# Generated test list headers

define gen_tests_hdr
	$(call msg,TEST-HDR,,$@)
	$(Q)(echo '/* Generated header, do not edit */';		\
		  sed -n -E 's/^void (serial_)?test_([a-zA-Z0-9_]+)\((void)?\).*/DEFINE_TEST(\2)/p'	\
		$(@D)/*.c | sort) > $@
endef

prog_tests/tests.h: $(wildcard prog_tests/*.c)
	$(gen_tests_hdr)

map_tests/tests.h: $(wildcard map_tests/*.c)
	$(gen_tests_hdr)

# Test runner instances, one sub-make each (see Makefile.runner).

# Share LLVM probe results with runners. Pass CC explicitly to
# runners so it does not affect libbpf's compiler selection.
export LLVM_LDLIBS LLVM_LDFLAGS

# Pass assembled flags on the command line so runner assignments
# cannot append them again.
RUNNER_MAKE := $(MAKE) --no-print-directory -f Makefile.runner	\
	       OUTPUT=$(OUTPUT) CC='$(CC)' CLANG='$(CLANG)'	\
	       CFLAGS='$(CFLAGS)' LDFLAGS='$(LDFLAGS)'

# Shared inputs prepared before runner sub-makes start.
# Also build veristat, which test_veristat executes.
RUNNER_PREREQS := $(INCLUDE_DIR)/vmlinux.h $(BPFOBJ) $(BPFTOOL)		\
		  $(TRUNNER_BPFTOOL) $(RESOLVE_BTFIDS)			\
		  $(OUTPUT)/veristat					\
		  $(VERIFY_SIG_HDR) $(PRIVATE_KEY) $(VERIFICATION_CERT)	\
		  $(LIBARENA_SKEL) $(LIBARENA_ASAN_SKEL)		\
		  prog_tests/tests.h map_tests/tests.h			\
		  $(RUNNER_OBJS) $(RUST_CORE)

# Runtime fixtures for each test_progs flavor.
RUNNER_EXTRA_FILES := $(OUTPUT)/urandom_read				\
		      $(OUTPUT)/liburandom_read.so			\
		      $(OUTPUT)/xdp_synproxy				\
		      $(OUTPUT)/sign-file				\
		      $(OUTPUT)/uprobe_multi				\
		      $(TEST_KMOD_TARGETS)				\
		      ima_setup.sh					\
		      $(VERIFY_SIG_SETUP)				\
		      $(wildcard progs/btf_dump_test_case_*.c)		\
		      $(wildcard progs/*.bpf.o)				\
		      bpftool_btf_dump_sorted.expected			\
		      bpftool_btf_dump_unsorted.expected

# Prepare fixtures in parallel with runner compilation.
# Copy into flavor and out-of-tree directories; the in-tree
# default runner uses the originals.
RUNNER_EXTRA_DIRS := $(addprefix $(OUTPUT)/,$(TEST_INST_SUBDIRS))
ifneq ($(realpath $(OUTPUT)),$(CURDIR))
RUNNER_EXTRA_DIRS += $(OUTPUT)
test_progs: $(OUTPUT)/runner-extras
else
test_progs: $(RUNNER_EXTRA_FILES)
endif
RUNNER_EXTRAS := $(addsuffix /runner-extras,$(RUNNER_EXTRA_DIRS))

.PHONY: $(RUNNER_EXTRAS)
$(RUNNER_EXTRAS): $(RUNNER_EXTRA_FILES)
	$(Q)mkdir -p $(@D)
	$(Q)rsync -aq $(if $(PERMISSIVE),--ignore-missing-args)	\
		$(RUNNER_EXTRA_FILES) $(@D)/

all: $(RUNNER_EXTRAS)
$(addprefix test_progs-,$(TEST_INST_SUBDIRS)): test_progs-%:		\
	$(OUTPUT)/%/runner-extras

# The main Makefile cannot tell whether $(OUTPUT)/test_progs is stale -
# only the runner sub-make knows its full dependency graph. FORCE makes
# the delegating rules below always run; their dependents still rebuild
# on mtime only.
FORCE:

# Build default BPF outputs here for standalone consumers and install.
# Standalone consumers depend on individual skeletons; unflavored
# runners depend on the complete set before their sub-makes start.
RDIR := $(OUTPUT)
FLAVOR :=
BINARY := test_progs
BPF_CC := $(CLANG)
BPF_CC_MSG := CLNG-BPF
BPF_SYS_INCLUDES := $(CLANG_SYS_INCLUDES)
BPF_CC_FLAGS := -O2 $(BPF_TARGET_ENDIAN) -mcpu=v3
BPF_DEFINES := -DENABLE_ATOMICS_TESTS
include Makefile.skel

DEFAULT_RUNNER_ARGS := RUNNER=test_progs FLAVOR= TESTS_DIR=prog_tests	\
	BPF_CC='$(BPF_CC)' BPF_CC_MSG=$(BPF_CC_MSG)			\
	BPF_SYS_INCLUDES='$(BPF_SYS_INCLUDES)'				\
	BPF_CC_FLAGS='$(BPF_CC_FLAGS)' BPF_DEFINES=$(BPF_DEFINES)
CLANG_RUNNER_ARGS := BPF_CC='$(CLANG)' BPF_CC_MSG=CLNG-BPF		\
	BPF_SYS_INCLUDES='$(CLANG_SYS_INCLUDES)'

$(OUTPUT)/test_progs: $(RUNNER_PREREQS) $(BPF_OBJS) $(ALL_SKELS) FORCE
	+$(Q)$(RUNNER_MAKE) $(DEFAULT_RUNNER_ARGS)

$(OUTPUT)/test_progs-no_alu32: $(RUNNER_PREREQS) FORCE
	+$(Q)$(RUNNER_MAKE) RUNNER=test_progs FLAVOR=no_alu32 TESTS_DIR=prog_tests \
		$(CLANG_RUNNER_ARGS) BPF_CC_FLAGS='-O2 $(BPF_TARGET_ENDIAN) -mcpu=v2'

ifneq ($(CLANG_CPUV4),)
$(OUTPUT)/test_progs-cpuv4: $(RUNNER_PREREQS) FORCE
	+$(Q)$(RUNNER_MAKE) RUNNER=test_progs FLAVOR=cpuv4 TESTS_DIR=prog_tests \
		$(CLANG_RUNNER_ARGS) BPF_CC_FLAGS='-O2 $(BPF_TARGET_ENDIAN) -mcpu=v4' \
		BPF_DEFINES=-DENABLE_ATOMICS_TESTS
endif

ifneq ($(BPF_GCC),)
# Sys includes come from the *host* gcc, not $(BPF_GCC), as the rule
# this replaces had it.
GCC_SYS_INCLUDES := $(call get_sys_includes,gcc,)

$(OUTPUT)/test_progs-bpf_gcc: $(RUNNER_PREREQS) FORCE
	+$(Q)$(RUNNER_MAKE) RUNNER=test_progs FLAVOR=bpf_gcc TESTS_DIR=prog_tests \
		BPF_CC='$(BPF_GCC)' BPF_CC_MSG=GCC-BPF				\
		BPF_SYS_INCLUDES='$(GCC_SYS_INCLUDES)'				\
		BPF_CC_FLAGS='-DBPF_NO_PRESERVE_ACCESS_INDEX -Wno-attributes -O2'
endif

# test_maps compiles map_tests/*.c against the default flavor's skeletons
$(OUTPUT)/test_maps: $(RUNNER_PREREQS) $(BPF_OBJS)	\
		     $(ALL_SKELS) FORCE
	+$(Q)$(RUNNER_MAKE) RUNNER=test_maps FLAVOR= TESTS_DIR=map_tests

TEST_GEN_FILES += $(BPF_OBJS)

# Define test_verifier test runner.
# It is much simpler than test_maps/test_progs and sufficiently different from
# them (e.g., test.h is using completely pattern), that it's worth just
# explicitly defining all the rules explicitly.
verifier/tests.h: verifier/*.c
	$(Q)( cd verifier/; \
		  echo '/* Generated header, do not edit */'; \
		  echo '#ifdef FILL_ARRAY'; \
		  ls *.c 2> /dev/null | sed -e 's@\(.*\)@#include \"\1\"@'; \
		  echo '#endif' \
		) > verifier/tests.h
$(OUTPUT)/test_verifier: test_verifier.c verifier/tests.h $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

# Keep xskxceiver independent from test_progs object dependencies.
$(OUTPUT)/xskxceiver: xskxceiver.c xsk.c network_helpers.c \
			   $(TOOLSDIR)/lib/find_bit.c prog_tests/test_xsk.c \
			   xskxceiver.h xsk.h network_helpers.h \
			   prog_tests/test_xsk.h test_progs.h bpf_util.h \
			   $(OUTPUT)/xsk_xdp_progs.skel.h $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/xdp_hw_metadata: xdp_hw_metadata.c xsk.c network_helpers.c \
			   $(TOOLSDIR)/lib/find_bit.c xdp_metadata.h \
			   xsk.h network_helpers.h test_progs.h bpf_util.h \
			   $(OUTPUT)/xdp_hw_metadata.skel.h $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/xdp_features: xdp_features.c network_helpers.c xdp_features.h \
			   network_helpers.h \
			   test_progs.h bpf_util.h $(OUTPUT)/xdp_features.skel.h \
			   $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

# Make sure we are able to include and link libbpf against c++.
CXXFLAGS += $(CFLAGS)
CXXFLAGS := $(subst -D_GNU_SOURCE=,,$(CXXFLAGS))
CXXFLAGS := $(subst -std=gnu11,-std=gnu++11,$(CXXFLAGS))
$(OUTPUT)/test_cpp: test_cpp.cpp $(OUTPUT)/test_core_extern.skel.h $(BPFOBJ)
	$(call msg,CXX,,$@)
	$(Q)$(CXX) $(CXXFLAGS) $(filter %.a %.o %.cpp,$^) $(LDLIBS) -o $@

# Benchmark runner. Every benchs/bench_*.c is compiled and linked in.
BENCH_OBJS := $(sort $(patsubst benchs/%.c,$(OUTPUT)/%.o,$(wildcard benchs/bench_*.c)))
$(OUTPUT)/bench_%.o: benchs/bench_%.c bench.h $(BPFOBJ)
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -O2 -c $(filter %.c,$^) $(LDLIBS) -o $@ $(call skip_on_fail,BENCH)
$(OUTPUT)/bench_rename.o: $(OUTPUT)/test_overhead.skel.h
$(OUTPUT)/bench_trigger.o: $(OUTPUT)/trigger_bench.skel.h
$(OUTPUT)/bench_ringbufs.o: $(OUTPUT)/ringbuf_bench.skel.h \
			    $(OUTPUT)/perfbuf_bench.skel.h
$(OUTPUT)/bench_bloom_filter_map.o: $(OUTPUT)/bloom_filter_bench.skel.h
$(OUTPUT)/bench_bpf_loop.o: $(OUTPUT)/bpf_loop_bench.skel.h
$(OUTPUT)/bench_bpf_for.o: $(OUTPUT)/bpf_for_bench.skel.h
$(OUTPUT)/bench_strncmp.o: $(OUTPUT)/strncmp_bench.skel.h
$(OUTPUT)/bench_bpf_hashmap_full_update.o: $(OUTPUT)/bpf_hashmap_full_update_bench.skel.h
$(OUTPUT)/bench_local_storage.o: $(OUTPUT)/local_storage_bench.skel.h
$(OUTPUT)/bench_local_storage_rcu_tasks_trace.o: $(OUTPUT)/local_storage_rcu_tasks_trace_bench.skel.h
$(OUTPUT)/bench_local_storage_create.o: $(OUTPUT)/bench_local_storage_create.skel.h
$(OUTPUT)/bench_bpf_hashmap_lookup.o: $(OUTPUT)/bpf_hashmap_lookup.skel.h
$(OUTPUT)/bench_htab_mem.o: $(OUTPUT)/htab_mem_bench.skel.h
$(OUTPUT)/bench_bpf_crypto.o: $(OUTPUT)/crypto_bench.skel.h
$(OUTPUT)/bench_sockmap.o: $(OUTPUT)/bench_sockmap_prog.skel.h
$(OUTPUT)/bench_lpm_trie_map.o: $(OUTPUT)/lpm_trie_bench.skel.h $(OUTPUT)/lpm_trie_map.skel.h
$(OUTPUT)/bench_bpf_nop.o: $(OUTPUT)/bpf_nop_bench.skel.h bench_bpf_timing.h
$(OUTPUT)/bench_xdp_lb.o: $(OUTPUT)/xdp_lb_bench.skel.h bench_bpf_timing.h
$(OUTPUT)/bench_libarena.o: $(LIBARENA_BENCH_SKEL)
$(OUTPUT)/bench_bpf_timing.o: bench_bpf_timing.h
$(OUTPUT)/bench.o: bench.h testing_helpers.h $(BPFOBJ)
$(OUTPUT)/bench: LDLIBS += -lm
$(OUTPUT)/bench: $(OUTPUT)/bench.o \
		 $(TESTING_HELPERS) \
		 $(TRACE_HELPERS) \
		 $(CGROUP_HELPERS) \
		 $(BENCH_OBJS) \
		 $(OUTPUT)/usdt_1.o \
		 $(OUTPUT)/usdt_2.o
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.a %.o,$^) $(LDLIBS) -o $@ $(call skip_on_fail,LINK,,(some benchmarks may have been skipped))

# This works around GCC warning about snprintf truncating strings like:
#
#   char a[PATH_MAX], b[PATH_MAX];
#   snprintf(a, "%s/foo", b);      // triggers -Wformat-truncation
$(OUTPUT)/veristat.o: CFLAGS += -Wno-format-truncation
$(OUTPUT)/veristat.o: $(BPFOBJ)
$(OUTPUT)/veristat: $(OUTPUT)/veristat.o
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.a %.o,$^) $(LDLIBS) -o $@

# Linking uprobe_multi can fail due to relocation overflows on mips.
$(OUTPUT)/uprobe_multi: CFLAGS += $(if $(filter mips, $(ARCH)),-mxgot)
$(OUTPUT)/uprobe_multi: uprobe_multi.c uprobe_multi.ld
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) -Wl,-T,uprobe_multi.ld -O0 $(LDFLAGS) 	\
		$(filter-out %.ld,$^) $(LDLIBS) -o $@

EXTRA_CLEAN := $(SCRATCH_DIR) $(HOST_SCRATCH_DIR)			\
	prog_tests/tests.h map_tests/tests.h verifier/tests.h		\
	feature bpftool $(TEST_KMOD_TARGETS)				\
	$(addprefix $(OUTPUT)/,*.o *.d *.skel.h *.lskel.h *.subskel.h	\
			       *.BTF *.BTF_ids *.BTF.base		\
			       no_alu32 cpuv4 bpf_gcc			\
			       liburandom_read.so)			\
	$(OUTPUT)/FEATURE-DUMP.selftests

.PHONY: docs docs-clean

# Delete partially updated (corrupted) files on error
.DELETE_ON_ERROR:

# When permissive, tell rsync to ignore missing source arguments so that
# partial builds do not abort installation.
ifneq ($(PERMISSIVE),)
override define INSTALL_SINGLE_RULE
	$(if $(INSTALL_LIST),@mkdir -p $(INSTALL_PATH))
	$(if $(INSTALL_LIST),rsync -a --copy-unsafe-links --ignore-missing-args $(INSTALL_LIST) $(INSTALL_PATH)/)
endef
endif

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	@mkdir -p $(INSTALL_PATH)/tools/sbin
	@rsync -a $(if $(PERMISSIVE),--ignore-missing-args) $(TRUNNER_BPFTOOL) $(INSTALL_PATH)/tools/sbin/
	@rsync -a $(if $(PERMISSIVE),--ignore-missing-args) $(OUTPUT)/*.BTF $(INSTALL_PATH)/
	@for DIR in $(TEST_INST_SUBDIRS); do				  \
		mkdir -p $(INSTALL_PATH)/$$DIR;				  \
		rsync -a $(if $(PERMISSIVE),--ignore-missing-args)	  \
			$(OUTPUT)/$$DIR/*.bpf.o $(OUTPUT)/$$DIR/*.BTF \
			$(INSTALL_PATH)/$$DIR;				  \
	done
endef

libarena: $(LIBARENA_SKEL)

ifneq ($(CLANG_HAS_ARENA_ASAN),)
libarena_asan: $(LIBARENA_ASAN_SKEL)
endif
