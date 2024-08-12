// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/statx.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <sys/types.h>
#include <linux/log2.h>

static size_t statx__scnprintf_mask(unsigned long mask, char *bf, size_t size, bool show_prefix)
{
	#include "trace/beauty/generated/statx_mask_array.c"
	static DEFINE_STRARRAY(statx_mask, "STATX_");
	return strarray__scnprintf_flags(&strarray__statx_mask, bf, size, show_prefix, mask);
}

size_t syscall_arg__scnprintf_statx_mask(char *bf, size_t size, struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	int mask = arg->val;

	return statx__scnprintf_mask(mask, bf, size, show_prefix);
}
