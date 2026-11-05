// SPDX-License-Identifier: GPL-2.0
/* Two functions contribute to one special section; only one is patched. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int other(int x)
{
	asm volatile(
		"2:\n\t"
		".pushsection	.kcfi_traps, \"a\"\n\t"
		".balign	4\n\t"
		".long		2b - .\n\t"
		".popsection\n\t");
	return x * 5;
}

int target(int x)
{
	asm volatile(
		"1:\n\t"
		".pushsection	.kcfi_traps, \"a\"\n\t"
		".balign	4\n\t"
		".long		1b - .\n\t"
		".popsection\n\t");
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
