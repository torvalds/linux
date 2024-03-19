// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/fs_at_flags.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <sys/types.h>
#include <linux/log2.h>

#include "trace/beauty/generated/fs_at_flags_array.c"
static DEFINE_STRARRAY(fs_at_flags, "AT_");

static size_t fs_at__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
	return strarray__scnprintf_flags(&strarray__fs_at_flags, bf, size, show_prefix, flags);
}

size_t syscall_arg__scnprintf_fs_at_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	int flags = arg->val;

	return fs_at__scnprintf_flags(flags, bf, size, show_prefix);
}
