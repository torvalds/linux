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

export CFLAGS += -I$(OUTPUT)/include -Iinclude -Wall -g -O2 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

OUTPUT_FORMAT = $(shell $(LD) -r -print-output-format)

ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64 elf32-i386 elf64-x86-64-freebsd elf32-littlearm))
  OUTPUT_DEF = $(shell echo | $(CC) -dM -E -)
  CFLAGS   += -fPIC -pthread
  ifeq (,$(filter $(OUTPUT_DEF),__ANDROID__))
    LDLIBS   += -lrt -lpthread
  endif
  export CONFIG_AUTO_LKL_POSIX_HOST=y
  CFLAGS += -DCONFIG_AUTO_LKL_POSIX_HOST

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
else ifneq (,$(filter $(OUTPUT_FORMAT),pe-i386 pe-x86-64 ))
  KOPT      = "KALLSYMS_EXTRA_PASS=1"
  LDLIBS   += -lws2_32
  EXESUF   := .exe
  SOSUF    := .dll
  export CONFIG_AUTO_LKL_NT_HOST=y
  CFLAGS += -DCONFIG_AUTO_LKL_NT_HOST
  ifneq (,$(filter $(OUTPUT_FORMAT),pe-x86-64))
      CFLAGS += -Wl,--enable-auto-image-base -Wl,--image-base -Wl,0x10000000 -Wl,--out-implib=$(OUTPUT)liblkl.dll.a -Wl,--export-all-symbols -Wl,--enable-auto-import
	  LDFLAGS +=-Wl,--image-base -Wl,0x10000000 -Wl,--enable-auto-image-base -Wl,--out-implib=$(OUTPUT)liblkl.dll.a -Wl,--export-all-symbols -Wl,--enable-auto-import
  endif
else
  $(error Unrecognized platform: $(OUTPUT_FORMAT))
endif

ifneq ($(OUTPUT),)
OUTPUT := $(OUTPUT)/tools/lkl/
else
OUTPUT := $(CURDIR)/
endif
export OUTPUT

ifneq (,$(filter $(OUTPUT_FORMAT),pe-i386 pe-x86-64 ))
ALL_PROGRAMS :=
ALL_LIBRARIES := $(OUTPUT)liblkl.a $(OUTPUT)liblkl$(SOSUF)
else
ALL_PROGRAMS := $(OUTPUT)lklfuse$(EXESUF) $(OUTPUT)cptofs$(EXESUF) $(OUTPUT)cpfromfs$(EXESUF) $(OUTPUT)fs2tar$(EXESUF)
ALL_LIBRARIES := $(OUTPUT)liblkl.a $(OUTPUT)liblkl$(SOSUF) $(OUTPUT)liblkl-hijack$(SOSUF)
endif

static: $(OUTPUT)liblkl.a
shared: $(OUTPUT)liblkl$(SOSUF)
hijack: $(OUTPUT)liblkl-hijack$(SOSUF)

$(OUTPUT)liblkl$(SOSUF): $(OUTPUT)lib/lkl-in.o $(OUTPUT)lib/lkl.o
$(OUTPUT)liblkl$(SOSUF): LDFLAGS += -shared

$(OUTPUT)liblkl-hijack$(SOSUF): $(OUTPUT)lib/hijack/hijack-in.o $(OUTPUT)liblkl.a
$(OUTPUT)liblkl-hijack$(SOSUF): LDFLAGS += -shared -nodefaultlibs
$(OUTPUT)liblkl-hijack$(SOSUF): LDLIBS += -ldl

$(OUTPUT)lklfuse$(EXESUF): $(OUTPUT)lklfuse-in.o $(OUTPUT)liblkl.a
$(OUTPUT)lklfuse$(EXESUF): LDLIBS += -lfuse

$(OUTPUT)fs2tar$(EXESUF): $(OUTPUT)fs2tar-in.o $(OUTPUT)liblkl.a
$(OUTPUT)fs2tar$(EXESUF): LDLIBS += -larchive
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd pe-x86-64))
  $(OUTPUT)fs2tar$(EXESUF): LDLIBS += -largp
endif

$(OUTPUT)cptofs$(EXESUF): $(OUTPUT)cptofs-in.o $(OUTPUT)liblkl.a
ifneq (,$(filter $(OUTPUT_FORMAT),elf64-x86-64-freebsd))
  $(OUTPUT)cptofs$(EXESUF): LDLIBS += -largp
endif


TEST_TARGETS        := test valgrind gdb

$(OUTPUT)tests/boot: $(OUTPUT)tests/boot-in.o $(OUTPUT)liblkl.a
$(OUTPUT)tests/net-test: $(OUTPUT)tests/net-test-in.o $(OUTPUT)liblkl.a
$(TEST_TARGETS): $(OUTPUT)tests/boot $(OUTPUT)tests/net-test

# because of libdl, liblkl-hijack will not compile on windows
# fortunately, the test target will handle a missing libhijack.so correctly
ifeq (,$(filter $(OUTPUT_FORMAT),pe-i386 elf32-littlearm))
  test: liblkl-hijack$(SOSUF)
endif

$(OUTPUT)%-in.o: $(OUTPUT)lib/lkl.o FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(patsubst %/,%,$(dir $*)) obj=$(notdir $*)

$(OUTPUT)lib/lkl.o:
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) defconfig
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(OUTPUT)

$(OUTPUT)liblkl.a: $(OUTPUT)lib/lkl-in.o $(OUTPUT)lib/lkl.o
	$(QUIET_AR)$(AR) -rc $@ $^

$(OUTPUT)liblkl$(SOSUF) $(OUTPUT)liblkl-hijack$(SOSUF) $(OUTPUT)lklfuse$(EXESUF) $(OUTPUT)fs2tar$(EXESUF) $(OUTPUT)cptofs$(EXESUF) $(OUTPUT)tests/boot $(OUTPUT)tests/net-test:
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OUTPUT)cpfromfs$(EXESUF): cptofs$(EXESUF)
	$(Q)if ! [ -e $@ ]; then ln -s $< $@; fi

# because of argp and fuse, none of the binaries will compile on windows
# because of libdl, liblkl-hijack will not compile on windows
# arm-android neither for the moment
ifneq (,$(filter $(OUTPUT_FORMAT),pe-i386))
  all: $(filter-out $(OUTPUT)liblkl-hijack$(SOSUF), $(ALL_LIBRARIES))
else ifneq (,$(filter $(OUTPUT_DEF),__ANDROID__))
  all: $(filter-out $(OUTPUT)liblkl-hijack$(SOSUF), $(ALL_LIBRARIES))
else ifneq (,$(filter $(OUTPUT_FORMAT),elf32-littlearm))
  all: $(filter-out $(OUTPUT)liblkl-hijack$(SOSUF), $(ALL_LIBRARIES)) $(ALL_PROGRAMS)
else
  all: $(ALL_PROGRAMS) $(ALL_LIBRARIES)
endif

clean:
	$(call QUIET_CLEAN, objects)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd'\
	 -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, headers)$(RM) -r $(OUTPUT)/include/lkl/
	$(call QUIET_CLEAN, "host libraries")$(RM) $(OUTPUT)/liblkl.a liblkl$(SOSUF)
	$(call QUIET_CLEAN, "hijack library")$(RM) $(OUTPUT)/liblkl-hijack$(SOSUF)
	$(call QUIET_CLEAN, programs)$(RM) $(ALL_PROGRAMS)
	$(call QUIET_CLEAN, tests)$(RM) tests/boot tests/net-test

$(TEST_TARGETS):
	$(MAKE) -C tests $@


headers_install:
	$(call QUIET_INSTALL, headers) \
	    install -d $(DESTDIR)$(PREFIX)/include ; \
	    install -m 644 include/lkl.h include/lkl_host.h $(DESTDIR)$(PREFIX)/include ; \
	    cp -r $(OUTPUT)include/lkl $(DESTDIR)$(PREFIX)/include

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
.NOTPARALLEL : lib/lkl.o
