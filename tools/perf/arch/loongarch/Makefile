# SPDX-License-Identifier: GPL-2.0
PERF_HAVE_JITDUMP := 1
HAVE_KVM_STAT_SUPPORT := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/loongarch/include/generated/asm
header := $(out)/syscalls.c
incpath := $(srctree)/tools
sysdef := $(srctree)/tools/arch/loongarch/include/uapi/asm/unistd.h
sysprf := $(srctree)/tools/perf/arch/loongarch/entry/syscalls/
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
$(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sysdef) $(systbl)
	$(Q)$(SHELL) '$(systbl)' '$(CC)' '$(HOSTCC)' $(incpath) $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, loongarch) $(RM) $(header)

archheaders: $(header)
