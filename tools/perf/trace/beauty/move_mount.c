// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/move_mount.c
 *
 *  Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/log2.h>

static size_t move_mount__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/move_mount_flags_array.c"
       static DEFINE_STRARRAY(move_mount_flags, "MOVE_MOUNT_");

       return strarray__scnprintf_flags(&strarray__move_mount_flags, bf, size, show_prefix, flags);
}

size_t syscall_arg__scnprintf_move_mount_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	return move_mount__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}
