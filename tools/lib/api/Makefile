# SPDX-License-Identifier: GPL-2.0
include ../../scripts/Makefile.include
include ../../scripts/utilities.mak		# QUIET_CLEAN

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

CC ?= $(CROSS_COMPILE)gcc
AR ?= $(CROSS_COMPILE)ar
LD ?= $(CROSS_COMPILE)ld

MAKEFLAGS += --no-print-directory

LIBFILE = $(OUTPUT)libapi.a

CFLAGS := $(EXTRA_WARNINGS) $(EXTRA_CFLAGS)
CFLAGS += -ggdb3 -Wall -Wextra -std=gnu99 -U_FORTIFY_SOURCE -fPIC

ifeq ($(DEBUG),0)
ifeq ($(CC_NO_CLANG), 0)
  CFLAGS += -O3
else
  CFLAGS += -O6
endif
endif

ifeq ($(DEBUG),0)
  CFLAGS += -D_FORTIFY_SOURCE
endif

# Treat warnings as errors unless directed not to
ifneq ($(WERROR),0)
  CFLAGS += -Werror
endif

CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I$(srctree)/tools/lib/api
CFLAGS += -I$(srctree)/tools/include

RM = rm -f

API_IN := $(OUTPUT)libapi-in.o

all:

export srctree OUTPUT CC LD CFLAGS V
include $(srctree)/tools/build/Makefile.include

all: fixdep $(LIBFILE)

$(API_IN): FORCE
	@$(MAKE) $(build)=libapi

$(LIBFILE): $(API_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(API_IN)

clean:
	$(call QUIET_CLEAN, libapi) $(RM) $(LIBFILE); \
	find $(or $(OUTPUT),.) -name \*.o -or -name \*.o.cmd -or -name \*.o.d | xargs $(RM)

FORCE:

.PHONY: clean FORCE
