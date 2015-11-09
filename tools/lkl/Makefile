CFLAGS := -Iinclude -Wall -g

ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
endif

lib_source = $(filter-out %-host.c,$(wildcard lib/*.c))
source = $(wildcard tests/*.c)
ifneq (,$(filter $(shell $(LD) -r -print-output-format),elf64-x86-64 elf32-i386))
source += $(wildcard *.c)
lib_source += lib/posix-host.c
LDFLAGS += -lpthread -lrt
source += $(wildcard *.c)
execs =  cpfromfs
else ifeq ($(shell $(LD) -r -print-output-format),pe-i386)
lib_source += lib/nt-host.c
KOPT="KALLSYMS_EXTRA_PASS=1"
endif

ifeq ($(shell $(LD) -r -print-output-format),elf64-x86-64)
CFLAGS += -D_FILE_OFFSET_BITS=64
endif

lib_objs = $(patsubst %.c,%.o, $(lib_source))
objs = $(patsubst %.c,%.o, $(source))
execs += $(patsubst %.c,%, $(source))

all: lib/liblkl.a $(execs)

lib/liblkl.a: $(lib_objs) lib/lkl.o
	$(AR) -rc $@ $^

lib/lkl.o:
	$(MAKE) -C ../.. ARCH=lkl defconfig
	$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(PWD)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(lib_objs): lib/lkl.o
$(objs): lib/lkl.o
$(execs): lib/liblkl.a

clean:
	-rm -rf include/lkl/ lib/liblkl.a $(lib_objs) lib/lkl.o $(objs) $(execs)

fs2tar: LDFLAGS += -larchive
lklfuse: LDFLAGS += -lfuse
cpfromfs: cptofs
	if ! [ -e $@ ]; then ln -s $< $@; fi
