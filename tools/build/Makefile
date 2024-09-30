# SPDX-License-Identifier: GPL-2.0
ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

include $(srctree)/tools//scripts/Makefile.include

define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,LD,$(CROSS_COMPILE)ld)

export HOSTCC HOSTLD HOSTAR

ifeq ($(V),1)
  Q =
else
  Q = @
endif

export Q srctree CC LD

MAKEFLAGS := --no-print-directory
build     := -f $(srctree)/tools/build/Makefile.build dir=. obj

all: $(OUTPUT)fixdep

# Make sure there's anything to clean,
# feature contains check for existing OUTPUT
TMP_O := $(if $(OUTPUT),$(OUTPUT)feature/,./)

clean:
	$(call QUIET_CLEAN, fixdep)
	$(Q)find $(or $(OUTPUT),.) -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)rm -f $(OUTPUT)fixdep
	$(call QUIET_CLEAN, feature-detect)
ifneq ($(wildcard $(TMP_O)),)
	$(Q)$(MAKE) -C feature OUTPUT=$(TMP_O) clean >/dev/null
endif

$(OUTPUT)fixdep: $(srctree)/tools/build/fixdep.c
	$(QUIET_CC)$(HOSTCC) $(KBUILD_HOSTCFLAGS) $(KBUILD_HOSTLDFLAGS) -o $@ $<
