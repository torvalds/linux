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
  OUTPUT        := $(CURDIR)
else
  # subdir is used by the ../Makefile in $(call descend,)
  ifneq ($(subdir),)
    OUTPUT        := $(OUTPUT)/$(subdir)
  endif
endif

ifneq ($(patsubst %/,,$(lastword $(OUTPUT))),)
  OUTPUT        := $(OUTPUT)/
endif

RTLA		:= $(OUTPUT)rtla
RTLA_IN		:= $(RTLA)-in.o

VERSION		:= $(shell sh -c "make -sC ../../.. kernelversion | grep -v make")
DOCSRC		:= ../../../Documentation/tools/rtla/

FEATURE_TESTS	:= libtraceevent
FEATURE_TESTS	+= libtracefs
FEATURE_TESTS	+= libcpupower
FEATURE_DISPLAY	:= libtraceevent
FEATURE_DISPLAY	+= libtracefs
FEATURE_DISPLAY	+= libcpupower

ifeq ($(V),1)
  Q		=
else
  Q		= @
endif

all: $(RTLA)

include $(srctree)/tools/build/Makefile.include
include Makefile.rtla

# check for dependencies only on required targets
NON_CONFIG_TARGETS := clean install tarball doc doc_clean doc_install

config		:= 1
ifdef MAKECMDGOALS
ifeq ($(filter-out $(NON_CONFIG_TARGETS),$(MAKECMDGOALS)),)
 config		:= 0
endif
endif

ifeq ($(config),1)
  include $(srctree)/tools/build/Makefile.feature
  include Makefile.config
endif

CFLAGS		+= $(INCLUDES) $(LIB_INCLUDES)

export CFLAGS OUTPUT srctree

$(RTLA): $(RTLA_IN)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $(RTLA) $(RTLA_IN) $(EXTLIBS)

static: $(RTLA_IN)
	$(eval LDFLAGS += -static)
	$(QUIET_LINK)$(CC) -static $(LDFLAGS) -o $(RTLA)-static $(RTLA_IN)  $(EXTLIBS)

rtla.%: fixdep FORCE
	make -f $(srctree)/tools/build/Makefile.build dir=. $@

$(RTLA_IN): fixdep FORCE
	make $(build)=rtla

clean: doc_clean fixdep-clean
	$(call QUIET_CLEAN, rtla)
	$(Q)find . -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)rm -f rtla rtla-static fixdep FEATURE-DUMP rtla-*
	$(Q)rm -rf feature
.PHONY: FORCE clean
