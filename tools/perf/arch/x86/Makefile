# SPDX-License-Identifier: GPL-2.0
ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
endif
HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_ARCH_REGS_QUERY_REGISTER_OFFSET := 1
PERF_HAVE_JITDUMP := 1

###
# Syscall table generation
#

out    := $(OUTPUT)arch/x86/include/generated/asm
header := $(out)/syscalls_64.c
sys    := $(srctree)/tools/perf/arch/x86/entry/syscalls
systbl := $(sys)/syscalltbl.sh

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sys)/syscall_64.tbl $(systbl)
	$(Q)$(SHELL) '$(systbl)' $(sys)/syscall_64.tbl 'x86_64' > $@

clean::
	$(call QUIET_CLEAN, x86) $(RM) $(header)

archheaders: $(header)
