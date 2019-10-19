// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/pkey_alloc.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <linux/log2.h>

size_t strarray__scnprintf_flags(struct strarray *sa, char *bf, size_t size, bool show_prefix, unsigned long flags)
{
	int i, printed = 0;

	if (flags == 0) {
		const char *s = sa->entries[0];
		if (s)
			return scnprintf(bf, size, "%s%s", show_prefix ? sa->prefix : "", s);
		return scnprintf(bf, size, "%d", 0);
	}

	for (i = 1; i < sa->nr_entries; ++i) {
		unsigned long bit = 1UL << (i - 1);

		if (!(flags & bit))
			continue;

		if (printed != 0)
			printed += scnprintf(bf + printed, size - printed, "|");

		if (sa->entries[i] != NULL)
			printed += scnprintf(bf + printed, size - printed, "%s%s", show_prefix ? sa->prefix : "", sa->entries[i]);
		else
			printed += scnprintf(bf + printed, size - printed, "0x%#", bit);
	}

	return printed;
}

static size_t pkey_alloc__scnprintf_access_rights(int access_rights, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/pkey_alloc_access_rights_array.c"
	static DEFINE_STRARRAY(pkey_alloc_access_rights, "PKEY_");

	return strarray__scnprintf_flags(&strarray__pkey_alloc_access_rights, bf, size, show_prefix, access_rights);
}

size_t syscall_arg__scnprintf_pkey_alloc_access_rights(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long cmd = arg->val;

	return pkey_alloc__scnprintf_access_rights(cmd, bf, size, arg->show_string_prefix);
}
