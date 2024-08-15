# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64 -Wall -Werror -I../.. -I../../../../.. -I../../../../include
LDLIBS := -lpthread -lelf
TEST_GEN_PROGS := fuse_test fuse_daemon
TEST_GEN_FILES := \
	test_bpf.bpf \
	fd_bpf.bpf \
	fd.sh \

EXTRA_CLEAN := *.bpf
BPF_FLAGS = -Wall -Werror -O2 -g -emit-llvm \
	    -I ../../../../../include \
	    -idirafter /usr/lib/gcc/x86_64-linux-gnu/10/include \
	    -idirafter /usr/local/include \
	    -idirafter /usr/include/x86_64-linux-gnu \
	    -idirafter /usr/include \

include ../../lib.mk

# Put after include ../../lib.mk since that changes $(TEST_GEN_PROGS)
# Otherwise you get multiple targets, this becomes the default, and it's a mess
EXTRA_SOURCES := bpf_loader.c
$(TEST_GEN_PROGS) : $(EXTRA_SOURCES)

$(OUTPUT)/%.ir: %.c
	clang $(BPF_FLAGS) -c $< -o $@

$(OUTPUT)/%.bpf: $(OUTPUT)/%.ir
	llc -march=bpf -filetype=obj -o $@ $<

$(OUTPUT)/fd.sh: fd.txt
	cp $< $@
	chmod 755 $@

