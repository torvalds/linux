CFLAGS += $(KHDR_INCLUDES)
CFLAGS += -I$(top_srcdir)/tools/include

CFLAGS += -march=rv64gc_zicfilp_zicfiss -fcf-protection=full

# Check for zicfi* extensions needs cross compiler
# which is not set until lib.mk is included
ifeq ($(LLVM)$(CC),cc)
CC := $(CROSS_COMPILE)gcc
endif


ifeq ($(shell $(CC) $(CFLAGS) -nostdlib -xc /dev/null -o /dev/null > /dev/null 2>&1; echo $$?),0)
TEST_GEN_PROGS := cfitests

$(OUTPUT)/cfitests: cfitests.c shadowstack.c
	$(CC) -o$@ $(CFLAGS) $(LDFLAGS) $^
else

$(shell echo "Toolchain doesn't support CFI, skipping CFI kselftest." >&2)
endif

include ../../lib.mk
