// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/prctl.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <uapi/linux/prctl.h>

#include "trace/beauty/generated/prctl_option_array.c"

static size_t prctl__scnprintf_option(int option, char *bf, size_t size, bool show_prefix)
{
	static DEFINE_STRARRAY(prctl_options, "PR_");
	return strarray__scnprintf(&strarray__prctl_options, bf, size, "%d", show_prefix, option);
}

static size_t prctl__scnprintf_set_mm(int option, char *bf, size_t size, bool show_prefix)
{
	static DEFINE_STRARRAY(prctl_set_mm_options, "PR_SET_MM_");
	return strarray__scnprintf(&strarray__prctl_set_mm_options, bf, size, "%d", show_prefix, option);
}

size_t syscall_arg__scnprintf_prctl_arg2(char *bf, size_t size, struct syscall_arg *arg)
{
	int option = syscall_arg__val(arg, 0);

	if (option == PR_SET_MM)
		return prctl__scnprintf_set_mm(arg->val, bf, size, arg->show_string_prefix);
	/*
	 * We still don't grab the contents of pointers on entry or exit,
	 * so just print them as hex numbers
	 */
	if (option == PR_SET_NAME)
		return syscall_arg__scnprintf_hex(bf, size, arg);

	return syscall_arg__scnprintf_long(bf, size, arg);
}

size_t syscall_arg__scnprintf_prctl_arg3(char *bf, size_t size, struct syscall_arg *arg)
{
	int option = syscall_arg__val(arg, 0);

	if (option == PR_SET_MM)
		return syscall_arg__scnprintf_hex(bf, size, arg);

	return syscall_arg__scnprintf_long(bf, size, arg);
}

size_t syscall_arg__scnprintf_prctl_option(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long option = arg->val;
	enum {
                SPO_ARG2 = (1 << 1),
                SPO_ARG3 = (1 << 2),
                SPO_ARG4 = (1 << 3),
                SPO_ARG5 = (1 << 4),
                SPO_ARG6 = (1 << 5),
        };
	const u8 all_but2 = SPO_ARG3 | SPO_ARG4 | SPO_ARG5 | SPO_ARG6;
	const u8 all = SPO_ARG2 | all_but2;
	const u8 masks[] = {
		[PR_GET_DUMPABLE]	 = all,
		[PR_SET_DUMPABLE]	 = all_but2,
		[PR_SET_NAME]		 = all_but2,
		[PR_GET_CHILD_SUBREAPER] = all_but2,
		[PR_SET_CHILD_SUBREAPER] = all_but2,
		[PR_GET_SECUREBITS]	 = all,
		[PR_SET_SECUREBITS]	 = all_but2,
		[PR_SET_MM]		 = SPO_ARG4 | SPO_ARG5 | SPO_ARG6,
		[PR_GET_PDEATHSIG]	 = all,
		[PR_SET_PDEATHSIG]	 = all_but2,
	};

	if (option < ARRAY_SIZE(masks))
		arg->mask |= masks[option];

	return prctl__scnprintf_option(option, bf, size, arg->show_string_prefix);
}
