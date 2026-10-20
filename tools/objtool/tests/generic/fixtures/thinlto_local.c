// SPDX-License-Identifier: GPL-2.0
/*
 * Two translation units (TU_B selects the second) linked with ThinLTO.
 * Importing bump() promotes the file-local counter, renaming it
 * counter.llvm.<hash>.  The hash is content derived, so it differs between the
 * original and patched builds.
 */

#ifdef TU_B

extern int bump(void);

int other_entry(void)
{
	return bump() + bump();
}

#else

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

static int counter;

int bump(void)
{
	return ++counter;
}

int target(void)
{
#ifdef PATCHED
	return counter + 1;
#else
	return counter;
#endif
}

#endif
