// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/sync_file_range.c
 *
 *  Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/log2.h>
#include <uapi/linux/fs.h>

static size_t sync_file_range__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/sync_file_range_arrays.c"
       static DEFINE_STRARRAY(sync_file_range_flags, "SYNC_FILE_RANGE_");
       size_t printed = 0;

       if ((flags & SYNC_FILE_RANGE_WRITE_AND_WAIT) == SYNC_FILE_RANGE_WRITE_AND_WAIT) {
               printed += scnprintf(bf + printed, size - printed, "%s%s", show_prefix ? "SYNC_FILE_RANGE_" : "", "WRITE_AND_WAIT");
	       flags &= ~SYNC_FILE_RANGE_WRITE_AND_WAIT;
       }

       return printed + strarray__scnprintf_flags(&strarray__sync_file_range_flags, bf + printed, size - printed, show_prefix, flags);
}

size_t syscall_arg__scnprintf_sync_file_range_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	return sync_file_range__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}
