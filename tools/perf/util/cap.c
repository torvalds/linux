// SPDX-License-Identifier: GPL-2.0
/*
 * Capability utilities
 */

#ifdef HAVE_LIBCAP_SUPPORT

#include "cap.h"
#include <stdbool.h>
#include <sys/capability.h>

bool perf_cap__capable(cap_value_t cap)
{
	cap_flag_value_t val;
	cap_t caps = cap_get_proc();

	if (!caps)
		return false;

	if (cap_get_flag(caps, cap, CAP_EFFECTIVE, &val) != 0)
		val = CAP_CLEAR;

	if (cap_free(caps) != 0)
		return false;

	return val == CAP_SET;
}

#endif  /* HAVE_LIBCAP_SUPPORT */
