# SPDX-License-Identifier: GPL-2.0
ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
endif

# Syscall table generation for perf
out    := $(OUTPUT)arch/mips/include/generated/asm
header := $(out)/syscalls_n64.c
sysprf := $(srctree)/tools/perf/arch/mips/entry/syscalls
sysdef := $(sysprf)/syscall_n64.tbl
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
$(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, mips) $(RM) $(header)

archheaders: $(header)
