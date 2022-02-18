top_srcdir = ../../../..

include ../lib.mk

.PHONY: all clean

CAN_BUILD_X86_64 := $(shell ../x86/check_cc.sh $(CC) \
			    ../x86/trivial_64bit_program.c)

ifndef OBJCOPY
OBJCOPY := $(CROSS_COMPILE)objcopy
endif

INCLUDES := -I$(top_srcdir)/tools/include
HOST_CFLAGS := -Wall -Werror -g $(INCLUDES) -fPIC -z noexecstack
ENCL_CFLAGS := -Wall -Werror -static -nostdlib -nostartfiles -fPIC \
	       -fno-stack-protector -mrdrnd $(INCLUDES)

TEST_CUSTOM_PROGS := $(OUTPUT)/test_sgx

ifeq ($(CAN_BUILD_X86_64), 1)
all: $(TEST_CUSTOM_PROGS) $(OUTPUT)/test_encl.elf
endif

$(OUTPUT)/test_sgx: $(OUTPUT)/main.o \
		    $(OUTPUT)/load.o \
		    $(OUTPUT)/sigstruct.o \
		    $(OUTPUT)/call.o \
		    $(OUTPUT)/sign_key.o
	$(CC) $(HOST_CFLAGS) -o $@ $^ -lcrypto

$(OUTPUT)/main.o: main.c
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OUTPUT)/load.o: load.c
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OUTPUT)/sigstruct.o: sigstruct.c
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OUTPUT)/call.o: call.S
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OUTPUT)/sign_key.o: sign_key.S
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OUTPUT)/test_encl.elf: test_encl.lds test_encl.c test_encl_bootstrap.S
	$(CC) $(ENCL_CFLAGS) -T $^ -o $@ -Wl,--build-id=none

EXTRA_CLEAN := \
	$(OUTPUT)/test_encl.elf \
	$(OUTPUT)/load.o \
	$(OUTPUT)/call.o \
	$(OUTPUT)/main.o \
	$(OUTPUT)/sigstruct.o \
	$(OUTPUT)/test_sgx \
	$(OUTPUT)/test_sgx.o \
