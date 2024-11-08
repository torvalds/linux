# SPDX-License-Identifier: GPL-2.0
HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_JITDUMP := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/powerpc/include/generated/asm
header32 := $(out)/syscalls_32.c
header64 := $(out)/syscalls_64.c
sysprf := $(srctree)/tools/perf/arch/powerpc/entry/syscalls
sysdef := $(sysprf)/syscall.tbl
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
$(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header64): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' '64' $(sysdef) > $@

$(header32): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' '32' $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, powerpc) $(RM) $(header32) $(header64)

archheaders: $(header32) $(header64)
