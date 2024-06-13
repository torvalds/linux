// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/kcmp.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <sys/types.h>
#include <machine.h>
#include <uapi/linux/kcmp.h>

#include "trace/beauty/generated/kcmp_type_array.c"

size_t syscall_arg__scnprintf_kcmp_idx(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long fd = arg->val;
	int type = syscall_arg__val(arg, 2);
	pid_t pid;

	if (type != KCMP_FILE)
		return syscall_arg__scnprintf_long(bf, size, arg);

	pid = syscall_arg__val(arg, arg->idx == 3 ? 0 : 1); /* idx1 -> pid1, idx2 -> pid2 */
	return pid__scnprintf_fd(arg->trace, pid, fd, bf, size);
}

static size_t kcmp__scnprintf_type(int type, char *bf, size_t size, bool show_prefix)
{
	static DEFINE_STRARRAY(kcmp_types, "KCMP_");
	return strarray__scnprintf(&strarray__kcmp_types, bf, size, "%d", show_prefix, type);
}

size_t syscall_arg__scnprintf_kcmp_type(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long type = arg->val;

	if (type != KCMP_FILE)
		arg->mask |= (1 << 3) | (1 << 4); /* Ignore idx1 and idx2 */

	return kcmp__scnprintf_type(type, bf, size, arg->show_string_prefix);
}
