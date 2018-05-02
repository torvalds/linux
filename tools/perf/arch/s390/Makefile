ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
endif
HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_ARCH_REGS_QUERY_REGISTER_OFFSET := 1

#
# Syscall table generation for perf
#

out    := $(OUTPUT)arch/s390/include/generated/asm
header := $(out)/syscalls_64.c
syskrn := $(srctree)/arch/s390/kernel/syscalls/syscall.tbl
sysprf := $(srctree)/tools/perf/arch/s390/entry/syscalls
sysdef := $(sysprf)/syscall.tbl
systbl := $(sysprf)/mksyscalltbl

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)')

$(header): $(sysdef) $(systbl)
	@(test -d ../../kernel -a -d ../../tools -a -d ../perf && ( \
        (diff -B $(sysdef) $(syskrn) >/dev/null) \
        || echo "Warning: Kernel ABI header at '$(sysdef)' differs from latest version at '$(syskrn)'" >&2 )) || true
	$(Q)$(SHELL) '$(systbl)' $(sysdef) > $@

clean::
	$(call QUIET_CLEAN, s390) $(RM) $(header)

archheaders: $(header)
