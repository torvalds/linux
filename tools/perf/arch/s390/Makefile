# SPDX-License-Identifier: GPL-2.0-only
HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_JITDUMP := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/s390/include/generated/asm
header := $(out)/syscalls_64.c
sysprf := $(srctree)/tools/perf/arch/s390/entry/syscalls
sysdef := $(sysprf)/syscall.tbl
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
$(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, s390) $(RM) $(header)

archheaders: $(header)
