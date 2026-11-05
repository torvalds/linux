// SPDX-License-Identifier: GPL-2.0
/*
 * Compiled twice and partially linked so the result has duplicate locals,
 * which is what symid_needed() requires.  dup_normal is in a live section,
 * dup_discarded in one the vmlinux link throws away.  DISCARDED_SEC selects
 * which discarded section, since there is more than one and each was its own
 * bug.
 */

#ifndef DISCARDED_SEC
#define DISCARDED_SEC ".exitcall.exit"
#endif

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

static int dup_normal = 1;

static void *dup_discarded
	__attribute__((section(DISCARDED_SEC), used)) = &dup_normal;

int FUNC_NAME(void)
{
	return dup_normal + (dup_discarded != (void *)0);
}
