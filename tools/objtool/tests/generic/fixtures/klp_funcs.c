// SPDX-License-Identifier: GPL-2.0
/*
 * Two changed functions and one untouched, so the patch's function list has a
 * length worth checking and something that must not appear in it.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int first(int x)
{
#ifdef PATCHED
	return x + 11;
#else
	return x + 1;
#endif
}

int second(int x)
{
#ifdef PATCHED
	return x + 22;
#else
	return x + 2;
#endif
}

int third(int x)
{
	return x + 3;
}
