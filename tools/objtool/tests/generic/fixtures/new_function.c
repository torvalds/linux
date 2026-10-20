// SPDX-License-Identifier: GPL-2.0
/* Function introduced by the patch.  noinline keeps it from being folded. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

#ifdef PATCHED
static __attribute__((noinline)) int klp_new_helper(int x)
{
	return x * 7;
}
#endif

int target(int x)
{
#ifdef PATCHED
	return klp_new_helper(x);
#else
	return x;
#endif
}
