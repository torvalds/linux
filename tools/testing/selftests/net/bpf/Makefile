# SPDX-License-Identifier: GPL-2.0

CLANG ?= clang
CCINCLUDE += -I../../bpf
CCINCLUDE += -I../../../lib
CCINCLUDE += -I../../../../../usr/include/

TEST_CUSTOM_PROGS = $(OUTPUT)/bpf/nat6to4.o
all: $(TEST_CUSTOM_PROGS)

$(OUTPUT)/%.o: %.c
	$(CLANG) -O2 -target bpf -c $< $(CCINCLUDE) -o $@

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS)
