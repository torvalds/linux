// SPDX-License-Identifier: GPL-2.0
/*
 * Data with an alignment stricter than its size.
 *
 * A cloned data section has to keep its sh_addralign.  The kernel has plenty
 * of data whose alignment is a correctness property rather than an
 * optimisation -- per-CPU variables, anything touched by an aligned SSE move,
 * cacheline-aligned locks -- and a clone that lands under-aligned faults or
 * silently shares a cacheline it was written to avoid.
 *
 * The object is new in the patched build, so klp diff has to clone it rather
 * than reference the kernel's copy.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

#ifdef PATCHED
int aligned_data[2] __attribute__((aligned(64))) = { 1, 2 };
#endif

int target(int x)
{
#ifdef PATCHED
	return x + aligned_data[0];
#else
	return x;
#endif
}
