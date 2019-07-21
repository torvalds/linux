# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/lib/bpf/Makefile

LIBPERF_VERSION = 0
LIBPERF_PATCHLEVEL = 0
LIBPERF_EXTRAVERSION = 1

MAKEFLAGS += --no-print-directory

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

include $(srctree)/tools/scripts/Makefile.include
include $(srctree)/tools/scripts/Makefile.arch

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -Wall
endif

INCLUDES = -I$(srctree)/tools/perf/lib/include -I$(srctree)/tools/include -I$(srctree)/tools/arch/$(ARCH)/include/ -I$(srctree)/tools/arch/$(ARCH)/include/uapi -I$(srctree)/tools/include/uapi

# Append required CFLAGS
override CFLAGS += $(EXTRA_WARNINGS)
override CFLAGS += -Werror -Wall
override CFLAGS += -fPIC
override CFLAGS += $(INCLUDES)
override CFLAGS += -fvisibility=hidden

all:

export srctree OUTPUT CC LD CFLAGS V
include $(srctree)/tools/build/Makefile.include

VERSION_SCRIPT := libperf.map

PATCHLEVEL    = $(LIBPERF_PATCHLEVEL)
EXTRAVERSION  = $(LIBPERF_EXTRAVERSION)
VERSION       = $(LIBPERF_VERSION).$(LIBPERF_PATCHLEVEL).$(LIBPERF_EXTRAVERSION)

LIBPERF_SO := $(OUTPUT)libperf.so.$(VERSION)
LIBPERF_A  := $(OUTPUT)libperf.a
LIBPERF_IN := $(OUTPUT)libperf-in.o

$(LIBPERF_IN): FORCE
	$(Q)$(MAKE) $(build)=libperf

$(LIBPERF_A): $(LIBPERF_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIBPERF_IN)

$(LIBPERF_SO): $(LIBPERF_IN)
	$(QUIET_LINK)$(CC) --shared -Wl,-soname,libperf.so \
                                    -Wl,--version-script=$(VERSION_SCRIPT) $^ -o $@
	@ln -sf $(@F) $(OUTPUT)libperf.so
	@ln -sf $(@F) $(OUTPUT)libperf.so.$(LIBPERF_VERSION)


libs: $(LIBPERF_A) $(LIBPERF_SO)

all: fixdep
	$(Q)$(MAKE) libs

clean:
	$(call QUIET_CLEAN, libperf) $(RM) $(LIBPERF_A) \
                *.o *~ *.a *.so *.so.$(VERSION) *.so.$(LIBPERF_VERSION) .*.d .*.cmd LIBPERF-CFLAGS

FORCE:

.PHONY: all install clean FORCE
