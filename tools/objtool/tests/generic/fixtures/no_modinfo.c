// SPDX-License-Identifier: GPL-2.0
/* Deliberately has no .modinfo section. */

int target(int x)
{
#ifdef PATCHED
	return x + 2;
#else
	return x + 1;
#endif
}
