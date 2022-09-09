// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/x86_msr.c
 *
 *  Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"

#include "trace/beauty/generated/x86_arch_MSRs_array.c"

static DEFINE_STRARRAY(x86_MSRs, "MSR_");
static DEFINE_STRARRAY_OFFSET(x86_64_specific_MSRs, "MSR_", x86_64_specific_MSRs_offset);
static DEFINE_STRARRAY_OFFSET(x86_AMD_V_KVM_MSRs, "MSR_", x86_AMD_V_KVM_MSRs_offset);

static struct strarray *x86_MSRs_tables[] = {
	&strarray__x86_MSRs,
	&strarray__x86_64_specific_MSRs,
	&strarray__x86_AMD_V_KVM_MSRs,
};

static DEFINE_STRARRAYS(x86_MSRs_tables);

static size_t x86_MSR__scnprintf(unsigned long msr, char *bf, size_t size, bool show_prefix)
{
	return strarrays__scnprintf(&strarrays__x86_MSRs_tables, bf, size, "%#x", show_prefix, msr);
}

size_t syscall_arg__scnprintf_x86_MSR(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	return x86_MSR__scnprintf(flags, bf, size, arg->show_string_prefix);
}

bool syscall_arg__strtoul_x86_MSR(char *bf, size_t size, struct syscall_arg *arg __maybe_unused, u64 *ret)
{
	return strarrays__strtoul(&strarrays__x86_MSRs_tables, bf, size, ret);
}
