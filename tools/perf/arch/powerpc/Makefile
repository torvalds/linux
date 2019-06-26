# SPDX-License-Identifier: GPL-2.0
ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
endif

HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_ARCH_REGS_QUERY_REGISTER_OFFSET := 1
PERF_HAVE_JITDUMP := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/powerpc/include/generated/asm
header32 := $(out)/syscalls_32.c
header64 := $(out)/syscalls_64.c
syskrn := $(srctree)/arch/powerpc/kernel/syscalls/syscall.tbl
sysprf := $(srctree)/tools/perf/arch/powerpc/entry/syscalls
sysdef := $(sysprf)/syscall.tbl
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header64): $(sysdef) $(systbl)
	@(test -d ../../kernel -a -d ../../tools -a -d ../perf && ( \
	(diff -B $(sysdef) $(syskrn) >/dev/null) \
	|| echo "Warning: Kernel ABI header at '$(sysdef)' differs from latest version at '$(syskrn)'" >&2 )) || true
	$(Q)$(SHELL) '$(systbl)' '64' $(sysdef) > $@

$(header32): $(sysdef) $(systbl)
	@(test -d ../../kernel -a -d ../../tools -a -d ../perf && ( \
	(diff -B $(sysdef) $(syskrn) >/dev/null) \
	|| echo "Warning: Kernel ABI header at '$(sysdef)' differs from latest version at '$(syskrn)'" >&2 )) || true
	$(Q)$(SHELL) '$(systbl)' '32' $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, powerpc) $(RM) $(header32) $(header64)

archheaders: $(header32) $(header64)
