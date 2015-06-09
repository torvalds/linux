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

MAKEFLAGS += --no-print-directory

LIBFILE = $(OUTPUT)libapi.a

CFLAGS := $(EXTRA_WARNINGS) $(EXTRA_CFLAGS)
CFLAGS += -ggdb3 -Wall -Wextra -std=gnu99 -Werror -O6 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fPIC
CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

RM = rm -f

build  := -f $(srctree)/tools/build/Makefile.build dir=. obj
API_IN := $(OUTPUT)libapi-in.o

export srctree OUTPUT CC LD CFLAGS V

all: $(LIBFILE)

$(API_IN): FORCE
	@$(MAKE) $(build)=libapi

$(LIBFILE): $(API_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(API_IN)

clean:
	$(call QUIET_CLEAN, libapi) $(RM) $(LIBFILE); \
	find $(if $(OUTPUT),$(OUTPUT),.) -name \*.o | xargs $(RM)

FORCE:

.PHONY: clean FORCE
