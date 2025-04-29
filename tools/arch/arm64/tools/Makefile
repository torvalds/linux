# SPDX-License-Identifier: GPL-2.0

ifeq ($(top_srcdir),)
top_srcdir := $(patsubst %/,%,$(dir $(CURDIR)))
top_srcdir := $(patsubst %/,%,$(dir $(top_srcdir)))
top_srcdir := $(patsubst %/,%,$(dir $(top_srcdir)))
top_srcdir := $(patsubst %/,%,$(dir $(top_srcdir)))
endif

include $(top_srcdir)/tools/scripts/Makefile.include

AWK	?= awk
MKDIR	?= mkdir
RM	?= rm

arm64_tools_dir = $(top_srcdir)/arch/arm64/tools
arm64_sysreg_tbl = $(arm64_tools_dir)/sysreg
arm64_gen_sysreg = $(arm64_tools_dir)/gen-sysreg.awk
arm64_generated_dir = $(OUTPUT)arch/arm64/include/generated
arm64_sysreg_defs = $(arm64_generated_dir)/asm/sysreg-defs.h

all: $(arm64_sysreg_defs)
	@:

$(arm64_sysreg_defs): $(arm64_gen_sysreg) $(arm64_sysreg_tbl)
	$(Q)$(MKDIR) -p $(dir $@)
	$(QUIET_GEN)$(AWK) -f $^ > $@

clean:
	$(Q)$(RM) -rf $(arm64_generated_dir)

.PHONY: all clean
