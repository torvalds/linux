all:

top_srcdir = ../../../../
UNAME_M := $(shell uname -m)

LIBKVM = lib/assert.c lib/elf.c lib/io.c lib/kvm_util.c lib/sparsebit.c
LIBKVM_x86_64 = lib/x86.c

TEST_GEN_PROGS_x86_64 = set_sregs_test
TEST_GEN_PROGS_x86_64 += sync_regs_test

TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(UNAME_M))
LIBKVM += $(LIBKVM_$(UNAME_M))

INSTALL_HDR_PATH = $(top_srcdir)/usr
LINUX_HDR_PATH = $(INSTALL_HDR_PATH)/include/
CFLAGS += -O2 -g -I$(LINUX_HDR_PATH) -Iinclude -I$(<D)

# After inclusion, $(OUTPUT) is defined and
# $(TEST_GEN_PROGS) starts with $(OUTPUT)/
include ../lib.mk

STATIC_LIBS := $(OUTPUT)/libkvm.a
LIBKVM_OBJ := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBKVM))
EXTRA_CLEAN += $(LIBKVM_OBJ) $(STATIC_LIBS)

x := $(shell mkdir -p $(sort $(dir $(LIBKVM_OBJ))))
$(LIBKVM_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

$(OUTPUT)/libkvm.a: $(LIBKVM_OBJ)
	$(AR) crs $@ $^

$(LINUX_HDR_PATH):
	make -C $(top_srcdir) headers_install

all: $(STATIC_LIBS) $(LINUX_HDR_PATH)
$(TEST_GEN_PROGS): $(STATIC_LIBS)
$(TEST_GEN_PROGS) $(LIBKVM_OBJ): | $(LINUX_HDR_PATH)
