// SPDX-License-Identifier: GPL-2.0
/*
 * A function which the patch changes from static to non-static, and a variable
 * that goes the other way.  The names are unchanged; only the binding moves.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

/* noinline, or the static one is folded into its caller and has no symbol */
#ifdef PATCHED
__attribute__((noinline)) int flipped_up(int x)		/* was static */
#else
__attribute__((noinline)) static int flipped_up(int x)
#endif
{
	return x + 1;
}

#ifdef PATCHED
static volatile int flipped_down = 5;	/* was global */
#else
volatile int flipped_down = 5;
#endif

int caller(int x)
{
	flipped_down += x;
#ifdef PATCHED
	return flipped_up(x) + flipped_down + 2;
#else
	return flipped_up(x) + flipped_down + 1;
#endif
}
