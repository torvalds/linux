/*
 * trace/beauty/pkey_alloc.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <linux/log2.h>

static size_t pkey_alloc__scnprintf_access_rights(int access_rights, char *bf, size_t size)
{
	int i, printed = 0;

#include "trace/beauty/generated/pkey_alloc_access_rights_array.c"
	static DEFINE_STRARRAY(pkey_alloc_access_rights);

	if (access_rights == 0) {
		const char *s = strarray__pkey_alloc_access_rights.entries[0];
		if (s)
			return scnprintf(bf, size, "%s", s);
		return scnprintf(bf, size, "%d", 0);
	}

	for (i = 1; i < strarray__pkey_alloc_access_rights.nr_entries; ++i) {
		int bit = 1 << (i - 1);

		if (!(access_rights & bit))
			continue;

		if (printed != 0)
			printed += scnprintf(bf + printed, size - printed, "|");

		if (strarray__pkey_alloc_access_rights.entries[i] != NULL)
			printed += scnprintf(bf + printed, size - printed, "%s", strarray__pkey_alloc_access_rights.entries[i]);
		else
			printed += scnprintf(bf + printed, size - printed, "0x%#", bit);
	}

	return printed;
}

size_t syscall_arg__scnprintf_pkey_alloc_access_rights(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long cmd = arg->val;

	return pkey_alloc__scnprintf_access_rights(cmd, bf, size);
}
