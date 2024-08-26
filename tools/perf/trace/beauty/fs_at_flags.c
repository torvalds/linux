// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/fs_at_flags.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <sys/types.h>
#include <linux/fcntl.h>
#include <linux/log2.h>

/*
 * uapi/linux/fcntl.h does not keep a copy in tools headers directory,
 * for system with kernel versions before v5.8, need to sync AT_EACCESS macro.
 */
#ifndef AT_EACCESS
#define AT_EACCESS 0x200
#endif

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

static size_t faccessat2__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
	int printed = 0;

	// AT_EACCESS is the same as AT_REMOVEDIR, that is in fs_at_flags_array,
	// special case it here.
	if (flags & AT_EACCESS) {
		flags &= ~AT_EACCESS;
		printed += scnprintf(bf + printed, size - printed, "%sEACCESS%s",
				     show_prefix ? strarray__fs_at_flags.prefix : "", flags ? "|" : "");
	}

	return strarray__scnprintf_flags(&strarray__fs_at_flags, bf + printed, size - printed, show_prefix, flags);
}

size_t syscall_arg__scnprintf_faccessat2_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	int flags = arg->val;

	return faccessat2__scnprintf_flags(flags, bf, size, show_prefix);
}
