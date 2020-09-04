// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../special.h"
#include "../../builtin.h"

#define X86_FEATURE_POPCNT (4 * 32 + 23)
#define X86_FEATURE_SMAP   (9 * 32 + 20)

void arch_handle_alternative(unsigned short feature, struct special_alt *alt)
{
	switch (feature) {
	case X86_FEATURE_SMAP:
		/*
		 * If UACCESS validation is enabled; force that alternative;
		 * otherwise force it the other way.
		 *
		 * What we want to avoid is having both the original and the
		 * alternative code flow at the same time, in that case we can
		 * find paths that see the STAC but take the NOP instead of
		 * CLAC and the other way around.
		 */
		if (uaccess)
			alt->skip_orig = true;
		else
			alt->skip_alt = true;
		break;
	case X86_FEATURE_POPCNT:
		/*
		 * It has been requested that we don't validate the !POPCNT
		 * feature path which is a "very very small percentage of
		 * machines".
		 */
		alt->skip_orig = true;
		break;
	default:
		break;
	}
}
