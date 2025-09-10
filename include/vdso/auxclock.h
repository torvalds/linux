/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VDSO_AUXCLOCK_H
#define _VDSO_AUXCLOCK_H

#include <uapi/linux/time.h>
#include <uapi/linux/types.h>

static __always_inline u64 aux_clock_resolution_ns(void)
{
	return 1;
}

#endif /* _VDSO_AUXCLOCK_H */
