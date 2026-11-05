// SPDX-License-Identifier: GPL-2.0
/*
 * A function the patch deletes, along with its only caller's use of it.  The
 * original has a symbol which the patched object simply does not, so there is
 * nothing to correlate it against.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

#ifndef PATCHED
int going_away(int x)
{
	return x + 7;
}
#endif

int caller(int x)
{
#ifdef PATCHED
	return x + 1;
#else
	return going_away(x);
#endif
}
