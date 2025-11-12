# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include
include ../scripts/Makefile.arch

ifeq ($(SRCARCH),x86)
	BUILD_ORC    := y
	ARCH_HAS_KLP := y
endif

ifeq ($(SRCARCH),loongarch)
	BUILD_ORC	   := y
endif

ifeq ($(ARCH_HAS_KLP),y)
	HAVE_XXHASH = $(shell printf "$(pound)include <xxhash.h>\nXXH3_state_t *state;int main() {}" | \
		      $(HOSTCC) -xc - -o /dev/null -lxxhash 2> /dev/null && echo y || echo n)
	ifeq ($(HAVE_XXHASH),y)
		BUILD_KLP	 := y
		LIBXXHASH_CFLAGS := $(shell $(HOSTPKG_CONFIG) libxxhash --cflags 2>/dev/null) \
				    -DBUILD_KLP
		LIBXXHASH_LIBS   := $(shell $(HOSTPKG_CONFIG) libxxhash --libs 2>/dev/null || echo -lxxhash)
	endif
endif

export BUILD_ORC BUILD_KLP

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

LIBSUBCMD_DIR = $(srctree)/tools/lib/subcmd/
ifneq ($(OUTPUT),)
  LIBSUBCMD_OUTPUT = $(abspath $(OUTPUT))/libsubcmd
else
  LIBSUBCMD_OUTPUT = $(CURDIR)/libsubcmd
endif
LIBSUBCMD = $(LIBSUBCMD_OUTPUT)/libsubcmd.a

OBJTOOL    := $(OUTPUT)objtool
OBJTOOL_IN := $(OBJTOOL)-in.o

LIBELF_FLAGS := $(shell $(HOSTPKG_CONFIG) libelf --cflags 2>/dev/null)
LIBELF_LIBS  := $(shell $(HOSTPKG_CONFIG) libelf --libs 2>/dev/null || echo -lelf)

all: $(OBJTOOL)

WARNINGS := -Werror -Wall -Wextra -Wmissing-prototypes			\
	    -Wmissing-declarations -Wwrite-strings			\
	    -Wno-implicit-fallthrough -Wno-sign-compare			\
	    -Wno-unused-parameter

INCLUDES := -I$(srctree)/tools/include \
	    -I$(srctree)/tools/include/uapi \
	    -I$(srctree)/tools/arch/$(HOSTARCH)/include/uapi \
	    -I$(srctree)/tools/arch/$(SRCARCH)/include	\
	    -I$(srctree)/tools/objtool/include \
	    -I$(srctree)/tools/objtool/arch/$(SRCARCH)/include \
	    -I$(LIBSUBCMD_OUTPUT)/include

OBJTOOL_CFLAGS  := -std=gnu11 -fomit-frame-pointer -O2 -g $(WARNINGS)	\
		   $(INCLUDES) $(LIBELF_FLAGS) $(LIBXXHASH_CFLAGS) $(HOSTCFLAGS)

OBJTOOL_LDFLAGS := $(LIBSUBCMD) $(LIBELF_LIBS) $(LIBXXHASH_LIBS) $(HOSTLDFLAGS)

# Allow old libelf to be used:
elfshdr := $(shell echo '$(pound)include <libelf.h>' | $(HOSTCC) $(OBJTOOL_CFLAGS) -x c -E - 2>/dev/null | grep elf_getshdr)
OBJTOOL_CFLAGS += $(if $(elfshdr),,-DLIBELF_USE_DEPRECATED)

# Always want host compilation.
HOST_OVERRIDES := CC="$(HOSTCC)" LD="$(HOSTLD)" AR="$(HOSTAR)"

AWK = awk
MKDIR = mkdir

export srctree OUTPUT CFLAGS SRCARCH AWK
include $(srctree)/tools/build/Makefile.include

$(OBJTOOL_IN): fixdep $(LIBSUBCMD) FORCE
	$(Q)$(CONFIG_SHELL) ./sync-check.sh
	$(Q)$(MAKE) $(build)=objtool $(HOST_OVERRIDES) CFLAGS="$(OBJTOOL_CFLAGS)" \
		LDFLAGS="$(OBJTOOL_LDFLAGS)"


$(OBJTOOL): $(LIBSUBCMD) $(OBJTOOL_IN)
	$(QUIET_LINK)$(HOSTCC) $(OBJTOOL_IN) $(OBJTOOL_LDFLAGS) -o $@


$(LIBSUBCMD_OUTPUT):
	$(Q)$(MKDIR) -p $@

$(LIBSUBCMD): fixdep $(LIBSUBCMD_OUTPUT) FORCE
	$(Q)$(MAKE) -C $(LIBSUBCMD_DIR) O=$(LIBSUBCMD_OUTPUT) \
		DESTDIR=$(LIBSUBCMD_OUTPUT) prefix= subdir= \
		$(HOST_OVERRIDES) EXTRA_CFLAGS="$(OBJTOOL_CFLAGS)" \
		$@ install_headers

$(LIBSUBCMD)-clean:
	$(call QUIET_CLEAN, libsubcmd)
	$(Q)$(RM) -r -- $(LIBSUBCMD_OUTPUT)

clean: $(LIBSUBCMD)-clean
	$(call QUIET_CLEAN, objtool) $(RM) $(OBJTOOL)
	$(Q)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)$(RM) $(OUTPUT)arch/x86/lib/inat-tables.c $(OUTPUT)fixdep

FORCE:

.PHONY: clean FORCE
