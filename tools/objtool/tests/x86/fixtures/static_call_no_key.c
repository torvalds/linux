// SPDX-License-Identifier: GPL-2.0
/*
 * A static call to a trampoline whose key symbol this object cannot see.
 *
 * That is the normal situation for a module: __SCK__* keys are not exported,
 * and read-only access is granted at load time instead.  objtool's static call
 * handling has to accept it for any module, including a livepatch module built
 * by hand rather than by klp-build.
 *
 * LIVEPATCH adds the .modinfo tag which makes objtool treat this as a
 * livepatch module.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) =
#ifdef LIVEPATCH
	"\0livepatch=Y"
#endif
	"\0name=klp_testmod";

/*
 * The trampoline is undefined here, exactly as it is for a module calling a
 * static call defined in vmlinux.  No __SCK__klp_test_call accompanies it.
 */
extern void __SCT__klp_test_call(void);

int target(int x)
{
	__asm__ volatile("call __SCT__klp_test_call\n\t" ::: "memory");

	return x + 1;
}
