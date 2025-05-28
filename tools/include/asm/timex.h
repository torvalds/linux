/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_ASM_TIMEX_H
#define __TOOLS_LINUX_ASM_TIMEX_H

#include <time.h>

#define cycles_t clock_t

static inline cycles_t get_cycles(void)
{
	return clock();
}
#endif // __TOOLS_LINUX_ASM_TIMEX_H
