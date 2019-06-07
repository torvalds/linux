// SPDX-License-Identifier: LGPL-2.1
// Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "trace/beauty/beauty.h"

static size_t renameat2__scnprintf_flags(unsigned long flags, char *bf, size_t size, bool show_prefix)
{
#include "trace/beauty/generated/rename_flags_array.c"
       static DEFINE_STRARRAY(rename_flags, "RENAME_");

       return strarray__scnprintf_flags(&strarray__rename_flags, bf, size, show_prefix, flags);
}

size_t syscall_arg__scnprintf_renameat2_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long flags = arg->val;
	return renameat2__scnprintf_flags(flags, bf, size, arg->show_string_prefix);
}
