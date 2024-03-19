// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/statx.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <sys/types.h>
#include <linux/stat.h>

#ifndef STATX_MNT_ID
#define STATX_MNT_ID		0x00001000U
#endif
#ifndef STATX_DIOALIGN
#define STATX_DIOALIGN		0x00002000U
#endif
#ifndef STATX_MNT_ID_UNIQUE
#define STATX_MNT_ID_UNIQUE	0x00004000U
#endif

size_t syscall_arg__scnprintf_statx_mask(char *bf, size_t size, struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "STATX_";
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & STATX_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", show_prefix ? prefix : "", #n); \
		flags &= ~STATX_##n; \
	}

	P_FLAG(TYPE);
	P_FLAG(MODE);
	P_FLAG(NLINK);
	P_FLAG(UID);
	P_FLAG(GID);
	P_FLAG(ATIME);
	P_FLAG(MTIME);
	P_FLAG(CTIME);
	P_FLAG(INO);
	P_FLAG(SIZE);
	P_FLAG(BLOCKS);
	P_FLAG(BTIME);
	P_FLAG(MNT_ID);
	P_FLAG(DIOALIGN);
	P_FLAG(MNT_ID_UNIQUE);

#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}
