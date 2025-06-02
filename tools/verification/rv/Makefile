# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(srctree),)
  srctree	:= $(patsubst %/,%,$(dir $(CURDIR)))
  srctree	:= $(patsubst %/,%,$(dir $(srctree)))
  srctree	:= $(patsubst %/,%,$(dir $(srctree)))
endif

include $(srctree)/tools/scripts/Makefile.include

# O is an alias for OUTPUT
OUTPUT          := $(O)

ifeq ($(OUTPUT),)
  OUTPUT       := $(CURDIR)/
else
  # subdir is used by the ../Makefile in $(call descend,)
  ifneq ($(subdir),)
    OUTPUT        := $(OUTPUT)/$(subdir)
  endif
endif

ifneq ($(patsubst %/,,$(lastword $(OUTPUT))),)
  OUTPUT := $(OUTPUT)/
endif

RV		:= $(OUTPUT)rv
RV_IN		:= $(RV)-in.o

VERSION		:= $(shell sh -c "make -sC ../../.. kernelversion | grep -v make")
DOCSRC		:= ../../../Documentation/tools/rv/

FEATURE_TESTS	:= libtraceevent
FEATURE_TESTS	+= libtracefs
FEATURE_DISPLAY	:= libtraceevent
FEATURE_DISPLAY	+= libtracefs

all: $(RV)

include $(srctree)/tools/build/Makefile.include
include Makefile.rv

# check for dependencies only on required targets
NON_CONFIG_TARGETS := clean install doc doc_clean doc_install

config		:= 1
ifdef MAKECMDGOALS
  ifeq ($(filter-out $(NON_CONFIG_TARGETS),$(MAKECMDGOALS)),)
  config	:= 0
endif
endif

ifeq ($(config),1)
  include $(srctree)/tools/build/Makefile.feature
  include Makefile.config
endif

CFLAGS		+= $(INCLUDES) $(LIB_INCLUDES)

export CFLAGS OUTPUT srctree

$(RV): $(RV_IN)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $(RV) $(RV_IN) $(EXTLIBS)

static: $(RV_IN)
	$(eval LDFLAGS += -static)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $(RV)-static $(RV_IN) $(EXTLIBS)

rv.%: fixdep FORCE
	make -f $(srctree)/tools/build/Makefile.build dir=. $@

$(RV_IN): fixdep FORCE
	make $(build)=rv

clean: doc_clean fixdep-clean
	$(call QUIET_CLEAN, rv)
	$(Q)find . -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)rm -f rv rv-static fixdep FEATURE-DUMP rv-*
	$(Q)rm -rf feature

.PHONY: FORCE clean
