// SPDX-License-Identifier: GPL-2.0
/*
 * Static locals of three kinds, in one patched function.
 *
 * Most static locals must be correlated, so the patched code keeps using the
 * running kernel's copy.  Two kinds must not:
 *
 *   - anything in .data..once, the flag behind WARN_ONCE and friends.  Sharing
 *     it would mean a patch inherits "already warned" from before the patch.
 *   - the well-known names the kernel generates for such things (__warned,
 *     __key, __func__, ...), which are per-instance by nature.  gcc names them
 *     <var>.<id> and Clang <func>.<var>, so both spellings have to be caught.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

int target(int x)
{
	/*
	 * A .data..once variable whose name is *not* on the list below, so
	 * only the section can disqualify it.  Naming it __warned would let
	 * the name rule catch it and the section rule go untested.
	 */
	static int once_flag __attribute__((section(".data..once")));
	/* a never-correlate name, in an ordinary section */
	static int __key;
	/* and one that must be correlated */
	static int ordinary;

	if (!once_flag)
		once_flag = 1;
	__key += x;
	ordinary += x;

#ifdef PATCHED
	return __key + ordinary + once_flag + 2;
#else
	return __key + ordinary + once_flag + 1;
#endif
}
