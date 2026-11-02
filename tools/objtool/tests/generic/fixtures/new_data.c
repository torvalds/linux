// SPDX-License-Identifier: GPL-2.0
/* Data introduced by the patch. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

#ifdef PATCHED
/*
 * Not an arithmetic progression: { 1, 2, 3, 4 } indexed by x & 3 is something
 * a compiler can compute instead of load, and then target() has no reference
 * to the array and there is nothing for klp diff to carry.
 */
static const int klp_new_data[4] __attribute__((used)) = { 7, 3, 11, 5 };
#endif

int target(int x)
{
#ifdef PATCHED
	return x + klp_new_data[x & 3];
#else
	return x;
#endif
}
