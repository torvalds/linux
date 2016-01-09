CFLAGS := -Iinclude -Wall -g -O2 -Wextra -Wno-unused-parameter \
	  -Wno-missing-field-initializers -fno-strict-aliasing

ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
else
CC=gcc
endif

OUTPUT_FORMAT=$(shell $(LD) -r -print-output-format)

lib_source = $(filter-out %-host.c,$(wildcard lib/*.c))
source = $(wildcard tests/*.c)
lib = lib/liblkl.a
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf32-i386 elf64-x86-64-freebsd))
source += $(wildcard *.c)
lib_source += lib/posix-host.c
CFLAGS += -fPIC
LDFLAGS += -lpthread -lrt
execs =  cpfromfs
lib += lib/liblkl.so
lib += lib/liblkl-hijack.so
else ifeq ($(OUTPUT_FORMAT),pe-i386)
lib_source := $(filter-out virtio_net.c,$(lib_source))
lib_source += lib/nt-host.c
KOPT="KALLSYMS_EXTRA_PASS=1"
LDFLAGS += -lws2_32
else
$(error Unrecognized platform: $(OUTPUT_FORMAT))
endif

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf64-x86-64-freebsd))
CFLAGS += -D_FILE_OFFSET_BITS=64
endif

lib_objs = $(patsubst %.c,%.o, $(lib_source))
objs = $(patsubst %.c,%.o, $(source))
execs += $(patsubst %.c,%, $(source))

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64))
lib += lib/liblkl_hijack.so
hijack_objs = $(patsubst %.c,%.o,$(wildcard lib/hijack/*.c))
objs += $(hijack_objs)
endif


all: $(lib) $(execs)

lib/liblkl.a: $(lib_objs) lib/lkl.o
	$(AR) -rc $@ $^

lib/liblkl.so: $(lib_objs) lib/lkl.o
	$(CC) -o $@ -shared $^

lib/liblkl_hijack.so: LDFLAGS += -ldl
lib/liblkl_hijack.so: $(hijack_objs) lib/liblkl.a
	$(CC) -o $@ -shared $^  $(LDFLAGS)

# order of -lxxx again really matters for LD_PRELOAD
lib/liblkl-hijack.so: $(hijack_objs) Makefile lib/liblkl.so
	$(CC) -shared -nodefaultlibs $(lib_objs) -Llib -llkl $(hijack_objs) \
	-ldl -lpthread -lrt -o $@

lib/lkl.o:
	$(MAKE) -C ../.. ARCH=lkl defconfig
	$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(PWD)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(lib_objs): lib/lkl.o
$(objs): lib/lkl.o
$(execs): lib/liblkl.a

host-clean:
	-rm -rf include/lkl/ $(lib) $(lib_objs) lib/lkl.o $(objs) $(execs)

clean: host-clean
	$(MAKE) -C ../.. ARCH=lkl clean

fs2tar: LDFLAGS += -larchive
lklfuse: LDFLAGS += -lfuse
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd))
cptofs: LDFLAGS += -largp
fs2tar: LDFLAGS += -largp
endif
cpfromfs: cptofs
	if ! [ -e $@ ]; then ln -s $< $@; fi

test:
	$(MAKE) -C tests

valgrind:
	$(MAKE) -C tests valgrind
