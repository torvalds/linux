// SPDX-License-Identifier: GPL-2.0
/*
 * A static whose name recurs in every translation unit that includes it.
 * Compiled once for a single-copy object and twice, partially linked, for one
 * with duplicates -- which is the only case where sympos is non-zero.
 *
 * FUNC_NAME keeps the referencing functions distinct so both get patched.
 * Only the first copy carries .modinfo; two would be a second thing to
 * disambiguate and is not what this fixture is about.
 */

#ifndef FUNC_NAME
#define FUNC_NAME use_a
#endif

#ifndef NO_MODINFO
static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";
#endif

/* volatile so it survives as an STT_OBJECT rather than being folded away */
static volatile int dup_counter = 1;

int FUNC_NAME(int x)
{
	dup_counter += x;
#ifdef PATCHED
	return dup_counter + 1;
#else
	return dup_counter;
#endif
}
