CFLAGS := -Iinclude -Wall -g

ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
AR=$(CROSS_COMPILE)ar
endif

lib_source = $(filter-out %-host.c,$(wildcard lib/*.c))
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
