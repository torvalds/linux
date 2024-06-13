// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/mount_flags.c
 *
 *  Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <sys/mount.h>

static size_t mount__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/mount_flags_array.c"
	static DEFINE_STRARRAY(mount_flags, "MS_");

	return strarray__scnprintf_flags(&strarray__mount_flags, bf, size, show_prefix, flags);
}

unsigned long syscall_arg__mask_val_mount_flags(struct syscall_arg *arg __maybe_unused, unsigned long flags)
{
	// do_mount in fs/namespace.c:
	/*
	 * Pre-0.97 versions of mount() didn't have a flags word.  When the
	 * flags word was introduced its top half was required to have the
	 * magic value 0xC0ED, and this remained so until 2.4.0-test9.
	 * Therefore, if this magic number is present, it carries no
	 * information and must be discarded.
	 */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	return flags;
}

size_t syscall_arg__scnprintf_mount_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;

	return mount__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}
