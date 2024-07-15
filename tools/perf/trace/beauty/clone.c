// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/cone.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <linux/log2.h>
#include <sys/types.h>
#include <sched.h>

static size_t clone__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/clone_flags_array.c"
	static DEFINE_STRARRAY(clone_flags, "CLONE_");

	return strarray__scnprintf_flags(&strarray__clone_flags, bf, size, show_prefix, flags);
}

size_t syscall_arg__scnprintf_clone_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;
	enum syscall_clone_args {
		SCC_FLAGS	  = (1 << 0),
		SCC_CHILD_STACK	  = (1 << 1),
		SCC_PARENT_TIDPTR = (1 << 2),
		SCC_CHILD_TIDPTR  = (1 << 3),
		SCC_TLS		  = (1 << 4),
	};
	if (!(flags & CLONE_PARENT_SETTID))
		arg->mask |= SCC_PARENT_TIDPTR;

	if (!(flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)))
		arg->mask |= SCC_CHILD_TIDPTR;

	if (!(flags & CLONE_SETTLS))
		arg->mask |= SCC_TLS;

	return clone__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}
