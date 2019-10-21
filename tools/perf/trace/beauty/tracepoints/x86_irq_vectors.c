// SPDX-License-Identifier: LGPL-2.1
/*
 * trace/beauty/x86_irq_vectors.c
 *
 *  Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include "trace/beauty/beauty.h"

#include "trace/beauty/generated/x86_arch_irq_vectors_array.c"

static DEFINE_STRARRAY(x86_irq_vectors, "_VECTOR");

static size_t x86_irq_vectors__scnprintf(unsigned long vector, char *bf, size_t size, bool show_prefix)
{
	return strarray__scnprintf_suffix(&strarray__x86_irq_vectors, bf, size, "%#x", show_prefix, vector);
}

size_t syscall_arg__scnprintf_x86_irq_vectors(char *bf, size_t size, struct syscall_arg *arg)
{
	unsigned long vector = arg->val;

	return x86_irq_vectors__scnprintf(vector, bf, size, arg->show_string_prefix);
}

bool syscall_arg__strtoul_x86_irq_vectors(char *bf, size_t size, struct syscall_arg *arg __maybe_unused, u64 *ret)
{
	return strarray__strtoul(&strarray__x86_irq_vectors, bf, size, ret);
}
