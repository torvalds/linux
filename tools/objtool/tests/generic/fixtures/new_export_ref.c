// SPDX-License-Identifier: GPL-2.0
/*
 * A reference which only exists in the patched build.  The symbol has no twin
 * in the original object, so what klp diff may do with it depends entirely on
 * whether Module.symvers says it is exported, and by what.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

extern int newly_referenced(int x);

/*
 * A reference both builds have.  When Module.symvers says a module exports
 * this one, the original already depends on that module, which is what makes
 * a new reference to it safe -- the loader will not let the patched module
 * load without it.  EXISTING_DEP leaves it out, for the case where there is
 * no such dependency to inherit.
 */
extern int existing_dep(int x);

int target(int x)
{
#ifdef EXISTING_DEP
	int base = existing_dep(x);
#else
	int base = x;
#endif

#ifdef PATCHED
	return newly_referenced(base);
#else
	return base + 1;
#endif
}
