// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2022, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "trace/beauty/beauty.h"
#include <inttypes.h>
#include <time.h>

static size_t syscall_arg__scnprintf_augmented_timespec(struct syscall_arg *arg, char *bf, size_t size)
{
	struct timespec *ts = (struct timespec *)arg->augmented.args->value;

	return scnprintf(bf, size, "{ .tv_sec: %" PRIu64 ", .tv_nsec: %" PRIu64 " }", ts->tv_sec, ts->tv_nsec);
}

size_t syscall_arg__scnprintf_timespec(char *bf, size_t size, struct syscall_arg *arg)
{
	if (arg->augmented.args)
		return syscall_arg__scnprintf_augmented_timespec(arg, bf, size);

	return scnprintf(bf, size, "%#lx", arg->val);
}
