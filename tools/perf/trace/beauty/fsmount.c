// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/fsmount.c
 *
 *  Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/log2.h>
#include <uapi/linux/mount.h>

static size_t fsmount__scnprintf_attr_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/fsmount_arrays.c"
       static DEFINE_STRARRAY(fsmount_attr_flags, "MOUNT_ATTR_");
       size_t printed = 0;

       if ((flags & ~MOUNT_ATTR__ATIME) != 0)
	       printed += strarray__scnprintf_flags(&strarray__fsmount_attr_flags, bf, size, show_prefix, flags);

       if ((flags & MOUNT_ATTR__ATIME) == MOUNT_ATTR_RELATIME) {
	       printed += scnprintf(bf + printed, size - printed, "%s%s%s",
			            printed ? "|" : "", show_prefix ? "MOUNT_ATTR_" : "", "RELATIME");
       }

       return printed;
}

size_t syscall_arg__scnprintf_fsmount_attr_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	return fsmount__scnprintf_attr_flags(flags, bf, size, arg->show_string_prefix);
}
