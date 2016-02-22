include ../../scripts/Makefile.include
include ../../perf/config/utilities.mak		# QUIET_CLEAN

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RM = rm -f

MAKEFLAGS += --no-print-directory

LIBFILE = $(OUTPUT)libsubcmd.a

CFLAGS := $(EXTRA_WARNINGS) $(EXTRA_CFLAGS)
CFLAGS += -ggdb3 -Wall -Wextra -std=gnu99 -Werror -O6 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fPIC
CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

CFLAGS += -I$(srctree)/tools/include/
CFLAGS += -I$(srctree)/include/uapi
CFLAGS += -I$(srctree)/include

SUBCMD_IN := $(OUTPUT)libsubcmd-in.o

all:

export srctree OUTPUT CC LD CFLAGS V
include $(srctree)/tools/build/Makefile.include

all: fixdep $(LIBFILE)

$(SUBCMD_IN): FORCE
	@$(MAKE) $(build)=libsubcmd

$(LIBFILE): $(SUBCMD_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(SUBCMD_IN)

clean:
	$(call QUIET_CLEAN, libsubcmd) $(RM) $(LIBFILE); \
	find $(if $(OUTPUT),$(OUTPUT),.) -name \*.o -or -name \*.o.cmd -or -name \*.o.d | xargs $(RM)

FORCE:

.PHONY: clean FORCE
