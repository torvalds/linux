// SPDX-License-Identifier: GPL-2.0
/*
 * A function which calls out to another object.  MODNAME selects which object
 * this one is, so a test can make the caller a module and the callee's owner
 * something else.
 */

#ifndef MODNAME
#define MODNAME "vmlinux"
#endif

/* klp diff takes the object's module name from .modinfo */
static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=" MODNAME;

extern int other_mod_func(int x);

int target(int x)
{
#ifdef PATCHED
	return other_mod_func(x) + 2;
#else
	return other_mod_func(x) + 1;
#endif
}
