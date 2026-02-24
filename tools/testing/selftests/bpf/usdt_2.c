// SPDX-License-Identifier: GPL-2.0

#if defined(__x86_64__)

/*
 * Include usdt.h with default nop,nop5 instructions combo.
 */
#include "usdt.h"

__attribute__((aligned(16)))
void usdt_2(void)
{
	USDT(optimized_attach, usdt_2);
}

#endif
