# SPDX-License-Identifier: GPL-2.0
PERF_HAVE_JITDUMP := 1
HAVE_KVM_STAT_SUPPORT := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/arm64/include/generated/asm
header := $(out)/syscalls.c
incpath := $(srctree)/tools
sysdef := $(srctree)/tools/arch/arm64/include/uapi/asm/unistd.h
sysprf := $(srctree)/tools/perf/arch/arm64/entry/syscalls/
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
$(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' '$(CC)' '$(HOSTCC)' $(incpath) $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, arm64) $(RM) $(header)

archheaders: $(header)
