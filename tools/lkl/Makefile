CFLAGS := -Iinclude -Wall -g

ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
LD=$(CROSS_COMPILE)ld
endif

lib_source = $(filter-out %-host.c,$(wildcard lib/*.c))
ifneq (,$(filter $(shell $(LD) -r -print-output-format),elf64-x86-64 elf32-i386))
lib_source += lib/posix-host.c
LDFLAGS += -lpthread -lrt
endif

lib_objs = $(patsubst %.c,%.o, $(lib_source)) lib/lkl.o

all: lib/liblkl.a

lib/liblkl.a: $(lib_objs)
	$(AR) -rc $@ $^

lib/lkl.o:
	$(MAKE) -C ../.. ARCH=lkl defconfig
	$(MAKE) -C ../.. ARCH=lkl install INSTALL_PATH=$(PWD)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(lib_objs): lib/lkl.o
$(objs): lib/lkl.o
$(execs): lib/liblkl.a

clean:
	-rm -rf include/lkl/ lib/liblkl.a $(lib_objs)
