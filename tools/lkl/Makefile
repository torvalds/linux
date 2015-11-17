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
source += $(wildcard *.c)
execs =  cpfromfs
lib += lib/liblkl.so
else ifeq ($(OUTPUT_FORMAT),pe-i386)
lib_source += lib/nt-host.c
KOPT="KALLSYMS_EXTRA_PASS=1"
else
$(error Unrecognized platform: $(OUTPUT_FORMAT))
endif

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf64-x86-64-freebsd))
CFLAGS += -D_FILE_OFFSET_BITS=64
endif

lib_objs = $(patsubst %.c,%.o, $(lib_source))
objs = $(patsubst %.c,%.o, $(source))
execs += $(patsubst %.c,%, $(source))

all: $(lib) $(execs)

lib/liblkl.a: $(lib_objs) lib/lkl.o
	$(AR) -rc $@ $^

lib/liblkl.so: $(lib_objs) lib/lkl.o
	gcc -o $@ -shared $^

lib/lkl.o:
	$(MAKE) -C ../.. ARCH=lkl defconfig
	$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(PWD)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(lib_objs): lib/lkl.o
$(objs): lib/lkl.o
$(execs): lib/liblkl.a

clean:
	-rm -rf include/lkl/ $(lib) $(lib_objs) lib/lkl.o $(objs) $(execs)
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
