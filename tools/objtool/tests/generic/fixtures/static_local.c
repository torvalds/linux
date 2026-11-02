// SPDX-License-Identifier: GPL-2.0
/* Static local in a patched function. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int target(int x)
{
	static int counter;

	counter += 1;
#ifdef PATCHED
	return x + counter + 1;
#else
	return x + counter;
#endif
}
