// SPDX-License-Identifier: GPL-2.0
/* Patched function referencing data in an .init section. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/* volatile so the read cannot be folded into a constant, leaving no reference */
static volatile int init_only __attribute__((section(".init.data"), used)) = 5;

int target(int x)
{
#ifdef PATCHED
	return x + init_only + 1;
#else
	return x + init_only;
#endif
}
