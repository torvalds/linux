# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
# also do not print "Entering directory..." messages from make
.SUFFIXES:
MAKEFLAGS += -r --no-print-directory 

ifeq ($(V),1)
  Q =
else
  Q = @
endif

# default target
all:

-include ../scripts/Makefile.include

# By default we want to use gcc (as does Linux), especially as the clang
# cross toolchain does not use prefixed names
CC    := $(CROSS_COMPILE)gcc
LD    := $(CROSS_COMPILE)$(LD)
AR    := $(CROSS_COMPILE)$(AR)
export CC LD AR

EXESUF   := 
SOSUF    := .so

PREFIX   := /usr

ifeq (,$(srctree))
  srctree := $(patsubst %/,%,$(dir $(shell pwd)))
  srctree := $(patsubst %/,%,$(dir $(srctree)))
endif
export srctree


# Target build configuration

export CFLAGS += -Iinclude -Wall -g -O2 -Wextra -Wno-unused-parameter \
	  -Wno-missing-field-initializers -fno-strict-aliasing

OUTPUT_FORMAT = $(shell $(LD) -r -print-output-format)

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf32-i386 elf64-x86-64-freebsd elf32-littlearm))
  OUTPUT_DEF = $(shell echo | $(CC) -dM -E -)
  CFLAGS   += -fPIC -pthread
  ifeq (,$(filter $(OUTPUT_DEF),__ANDROID__))
    LDLIBS   += -lrt -lpthread
  endif
  export CONFIG_AUTO_LKL_POSIX_HOST=y

  # Intel DPDK configuration
  ifeq ($(dpdk),yes)
    export CONFIG_AUTO_LKL_VIRTIO_NET_DPDK=y
    RTE_SDK ?= $(shell pwd)/dpdk-2.2.0
    RTE_TARGET ?= build
    CFLAGS += -I$(RTE_SDK)/$(RTE_TARGET)/include -msse4.2 -mpopcnt
    CFLAGS += -DCONFIG_AUTO_LKL_VIRTIO_NET_DPDK
    LDFLAGS +=-L$(RTE_SDK)/$(RTE_TARGET)/lib
    LDFLAGS +=-Wl,--whole-archive -ldpdk -Wl,--no-whole-archive -lm -ldl
  endif
  # Virtual Distributed Ethernet configuration
  ifeq ($(vde),yes)
    export CONFIG_AUTO_LKL_VIRTIO_NET_VDE=y
    CFLAGS     += -DCONFIG_AUTO_LKL_VIRTIO_NET_VDE
    LDLIBS     += $(shell pkg-config --libs vdeplug)
  endif
else ifneq (,$(filter $(OUTPUT_FORMAT),pe-i386))
  KOPT      = "KALLSYMS_EXTRA_PASS=1"
  LDLIBS   += -lws2_32
  EXESUF   := .exe
  SOSUF    := .dll
  export CONFIG_AUTO_LKL_NT_HOST=y
else
  $(error Unrecognized platform: $(OUTPUT_FORMAT))
endif

ALL_PROGRAMS := lklfuse$(EXESUF) cptofs$(EXESUF) cpfromfs$(EXESUF) fs2tar$(EXESUF)
ALL_LIBRARIES := liblkl.a liblkl$(SOSUF) liblkl-hijack$(SOSUF)

static: liblkl.a
shared: liblkl$(SOSUF)
hijack: liblkl-hijack$(SOSUF)

liblkl$(SOSUF): lib/lkl-in.o lib/lkl.o
liblkl$(SOSUF): LDFLAGS += -shared

liblkl-hijack$(SOSUF): lib/hijack/hijack-in.o liblkl.a
liblkl-hijack$(SOSUF): LDFLAGS += -shared -nodefaultlibs
liblkl-hijack$(SOSUF): LDLIBS += -ldl

lklfuse$(EXESUF): lklfuse-in.o liblkl.a
lklfuse$(EXESUF): LDLIBS += -lfuse

fs2tar$(EXESUF): fs2tar-in.o liblkl.a
fs2tar$(EXESUF): LDLIBS += -larchive
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd))
  fs2tar$(EXESUF): LDLIBS += -largp
endif

cptofs$(EXESUF): cptofs-in.o liblkl.a
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd))
  cptofs$(EXESUF): LDLIBS += -largp
endif


TEST_TARGETS        := test valgrind gdb

tests/boot: tests/boot-in.o liblkl.a
tests/net-test: tests/net-test-in.o liblkl.a
$(TEST_TARGETS): tests/boot tests/net-test

# because of libdl, liblkl-hijack will not compile on windows
# fortunately, the test target will handle a missing libhijack.so correctly
ifeq (,$(filter $(OUTPUT_FORMAT),pe-i386 elf32-littlearm))
  test: liblkl-hijack$(SOSUF)
endif


%-in.o: lib/lkl.o FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(patsubst %/,%,$(dir $*)) obj=$(notdir $*)

lib/lkl.o:
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) defconfig
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(CURDIR)

liblkl.a: lib/lkl-in.o lib/lkl.o
	$(QUIET_AR)$(AR) -rc $@ $^

liblkl$(SOSUF) liblkl-hijack$(SOSUF) lklfuse$(EXESUF) fs2tar$(EXESUF) cptofs$(EXESUF) tests/boot tests/net-test:
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

cpfromfs$(EXESUF): cptofs$(EXESUF)
	$(Q)if ! [ -e $@ ]; then ln -s $< $@; fi

# because of argp and fuse, none of the binaries will compile on windows
# because of libdl, liblkl-hijack will not compile on windows
# arm-android neither for the moment
ifneq (,$(filter $(OUTPUT_FORMAT),pe-i386))
  all: $(filter-out liblkl-hijack$(SOSUF), $(ALL_LIBRARIES))
else ifneq (,$(filter $(OUTPUT_DEF),__ANDROID__))
  all: $(filter-out liblkl-hijack$(SOSUF), $(ALL_LIBRARIES))
else ifneq (,$(filter $(OUTPUT_FORMAT),elf32-littlearm))
  all: $(filter-out liblkl-hijack$(SOSUF), $(ALL_LIBRARIES)) $(ALL_PROGRAMS)
else
  all: $(ALL_PROGRAMS) $(ALL_LIBRARIES)
endif

clean:
	$(call QUIET_CLEAN, objects)find . -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, headers)$(RM) -r include/lkl/
	$(call QUIET_CLEAN, "host libraries")$(RM) liblkl.a liblkl$(SOSUF)
	$(call QUIET_CLEAN, "hijack library")$(RM) liblkl-hijack$(SOSUF)
	$(call QUIET_CLEAN, programs)$(RM) lklfuse$(EXESUF) fs2tar$(EXESUF) cptofs$(EXESUF) cpfromfs$(EXESUF)
	$(call QUIET_CLEAN, tests)$(RM) tests/boot tests/net-test

$(TEST_TARGETS):
	$(MAKE) -C tests $@


headers_install:
	$(call QUIET_INSTALL, headers) \
	    install -d $(DESTDIR)$(PREFIX)/include ; \
	    install -m 644 include/lkl.h include/lkl_host.h $(DESTDIR)$(PREFIX)/include ; \
	    cp -r include/lkl $(DESTDIR)$(PREFIX)/include

libraries_install: $(ALL_LIBRARIES)
	$(call QUIET_INSTALL, libraries) \
	    install -d $(DESTDIR)$(PREFIX)/lib ; \
	    install -m 644 $(ALL_LIBRARIES) $(DESTDIR)$(PREFIX)/lib

programs_install: $(ALL_PROGRAMS)
	$(call QUIET_INSTALL, programs) \
	    install -d $(DESTDIR)$(PREFIX)/bin ; \
	    install -m 755 $(ALL_PROGRAMS) $(DESTDIR)$(PREFIX)/bin

install: headers_install libraries_install programs_install


FORCE: ;
.PHONY: all clean $(TEST_TARGETS) FORCE
.PHONY: headers_install libraries_install programs_install install
