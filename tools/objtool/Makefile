include ../scripts/Makefile.include

ifndef ($(ARCH))
ARCH ?= $(shell uname -m)
ifeq ($(ARCH),x86_64)
ARCH := x86
endif
endif

# always use the host compiler
CC = gcc
LD = ld
AR = ar

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

SUBCMD_SRCDIR		= $(srctree)/tools/lib/subcmd/
LIBSUBCMD_OUTPUT	= $(if $(OUTPUT),$(OUTPUT),$(PWD)/)
LIBSUBCMD		= $(LIBSUBCMD_OUTPUT)libsubcmd.a

OBJTOOL    := $(OUTPUT)objtool
OBJTOOL_IN := $(OBJTOOL)-in.o

all: $(OBJTOOL)

INCLUDES := -I$(srctree)/tools/include
CFLAGS   += -Wall -Werror $(EXTRA_WARNINGS) -fomit-frame-pointer -O2 -g $(INCLUDES)
LDFLAGS  += -lelf $(LIBSUBCMD)

# Allow old libelf to be used:
elfshdr := $(shell echo '\#include <libelf.h>' | $(CC) $(CFLAGS) -x c -E - | grep elf_getshdr)
CFLAGS += $(if $(elfshdr),,-DLIBELF_USE_DEPRECATED)

AWK = awk
export srctree OUTPUT CFLAGS ARCH AWK
include $(srctree)/tools/build/Makefile.include

$(OBJTOOL_IN): fixdep FORCE
	@$(MAKE) $(build)=objtool

$(OBJTOOL): $(LIBSUBCMD) $(OBJTOOL_IN)
	@(test -d ../../kernel -a -d ../../tools -a -d ../objtool && (( \
	diff -I'^#include' arch/x86/insn/insn.c ../../arch/x86/lib/insn.c >/dev/null && \
	diff -I'^#include' arch/x86/insn/inat.c ../../arch/x86/lib/inat.c >/dev/null && \
	diff arch/x86/insn/x86-opcode-map.txt ../../arch/x86/lib/x86-opcode-map.txt >/dev/null && \
	diff arch/x86/insn/gen-insn-attr-x86.awk ../../arch/x86/tools/gen-insn-attr-x86.awk >/dev/null && \
	diff -I'^#include' arch/x86/insn/insn.h ../../arch/x86/include/asm/insn.h >/dev/null && \
	diff -I'^#include' arch/x86/insn/inat.h ../../arch/x86/include/asm/inat.h >/dev/null && \
	diff -I'^#include' arch/x86/insn/inat_types.h ../../arch/x86/include/asm/inat_types.h >/dev/null) \
	|| echo "Warning: objtool: x86 instruction decoder differs from kernel" >&2 )) || true
	$(QUIET_LINK)$(CC) $(OBJTOOL_IN) $(LDFLAGS) -o $@


$(LIBSUBCMD): fixdep FORCE
	$(Q)$(MAKE) -C $(SUBCMD_SRCDIR) OUTPUT=$(LIBSUBCMD_OUTPUT)

clean:
	$(call QUIET_CLEAN, objtool) $(RM) $(OBJTOOL)
	$(Q)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)$(RM) $(OUTPUT)arch/x86/insn/inat-tables.c $(OUTPUT)fixdep

FORCE:

.PHONY: clean FORCE
