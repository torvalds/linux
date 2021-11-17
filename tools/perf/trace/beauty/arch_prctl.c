// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/arch_prctl.c
 *
 *  Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>

#include "trace/beauty/generated/x86_arch_prctl_code_array.c"

static DEFINE_STRARRAY_OFFSET(x86_arch_prctl_codes_1, "ARCH_", x86_arch_prctl_codes_1_offset);
static DEFINE_STRARRAY_OFFSET(x86_arch_prctl_codes_2, "ARCH_", x86_arch_prctl_codes_2_offset);

static struct strarray *x86_arch_prctl_codes[] = {
	&strarray__x86_arch_prctl_codes_1,
	&strarray__x86_arch_prctl_codes_2,
};

static DEFINE_STRARRAYS(x86_arch_prctl_codes);

static size_t x86_arch_prctl__scnprintf_code(int option, char *bf, size_t size, bool show_prefix)
{
	return strarrays__scnprintf(&strarrays__x86_arch_prctl_codes, bf, size, "%#x", show_prefix, option);
}

size_t syscall_arg__scnprintf_x86_arch_prctl_code(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long code = arg->val;

	return x86_arch_prctl__scnprintf_code(code, bf, size, arg->show_string_prefix);
}
