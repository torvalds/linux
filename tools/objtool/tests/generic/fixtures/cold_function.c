// SPDX-License-Identifier: GPL-2.0
/* Function the compiler may split into a hot part and a foo.cold part. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

static void __attribute__((cold, noinline)) slow_path(int x)
{
	__asm__ volatile("" :: "r"(x));
}

int target(int x)
{
	if (__builtin_expect(x < 0, 0))
		slow_path(x);
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
