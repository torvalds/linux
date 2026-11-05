// SPDX-License-Identifier: GPL-2.0
/* One changed function and one unchanged function. */

/* klp diff takes the object's module name from .modinfo */
static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int untouched(int x)
{
	return x * 3;
}

int changed(int x)
{
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
