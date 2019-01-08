all:

top_srcdir = ../../../..
KSFT_KHDR_INSTALL := 1
UNAME_M := $(shell uname -m)

LIBKVM = lib/assert.c lib/elf.c lib/io.c lib/kvm_util.c lib/ucall.c lib/sparsebit.c
LIBKVM_x86_64 = lib/x86_64/processor.c lib/x86_64/vmx.c
LIBKVM_aarch64 = lib/aarch64/processor.c

TEST_GEN_PROGS_x86_64 = x86_64/platform_info_test
TEST_GEN_PROGS_x86_64 += x86_64/set_sregs_test
TEST_GEN_PROGS_x86_64 += x86_64/sync_regs_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_tsc_adjust_test
TEST_GEN_PROGS_x86_64 += x86_64/cr4_cpuid_sync_test
TEST_GEN_PROGS_x86_64 += x86_64/state_test
TEST_GEN_PROGS_x86_64 += x86_64/evmcs_test
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_cpuid
TEST_GEN_PROGS_x86_64 += dirty_log_test
TEST_GEN_PROGS_x86_64 += clear_dirty_log_test

TEST_GEN_PROGS_aarch64 += dirty_log_test
TEST_GEN_PROGS_aarch64 += clear_dirty_log_test

TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(UNAME_M))
LIBKVM += $(LIBKVM_$(UNAME_M))

INSTALL_HDR_PATH = $(top_srcdir)/usr
LINUX_HDR_PATH = $(INSTALL_HDR_PATH)/include/
LINUX_TOOL_INCLUDE = $(top_srcdir)/tools/include
CFLAGS += -O2 -g -std=gnu99 -I$(LINUX_TOOL_INCLUDE) -I$(LINUX_HDR_PATH) -Iinclude -I$(<D) -Iinclude/$(UNAME_M) -I..
LDFLAGS += -pthread

# After inclusion, $(OUTPUT) is defined and
# $(TEST_GEN_PROGS) starts with $(OUTPUT)/
include ../lib.mk

STATIC_LIBS := $(OUTPUT)/libkvm.a
LIBKVM_OBJ := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBKVM))
EXTRA_CLEAN += $(LIBKVM_OBJ) $(STATIC_LIBS) cscope.*

x := $(shell mkdir -p $(sort $(dir $(LIBKVM_OBJ))))
$(LIBKVM_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

$(OUTPUT)/libkvm.a: $(LIBKVM_OBJ)
	$(AR) crs $@ $^

all: $(STATIC_LIBS)
$(TEST_GEN_PROGS): $(STATIC_LIBS)

cscope: include_paths = $(LINUX_TOOL_INCLUDE) $(LINUX_HDR_PATH) include lib ..
cscope:
	$(RM) cscope.*
	(find $(include_paths) -name '*.h' \
		-exec realpath --relative-base=$(PWD) {} \;; \
	find . -name '*.c' \
		-exec realpath --relative-base=$(PWD) {} \;) | sort -u > cscope.files
	cscope -b
