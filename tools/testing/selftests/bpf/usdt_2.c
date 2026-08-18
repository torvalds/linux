// SPDX-License-Identifier: GPL-2.0

#if defined(__x86_64__)

/*
 * Include usdt.h with default nop,nop10 instructions combo.
 */
#include "usdt.h"

__attribute__((aligned(16)))
void usdt_2(void)
{
	USDT(optimized_attach, usdt_2);
}

static volatile unsigned long usdt_red_zone_arg1 = 0xDEADBEEF;
static volatile unsigned long usdt_red_zone_arg2 = 0xCAFEBABE;
static volatile unsigned long usdt_red_zone_arg3 = 0xFEEDFACE;

void __attribute__((noinline)) usdt_red_zone_trigger(void)
{
	unsigned long a1 = usdt_red_zone_arg1;
	unsigned long a2 = usdt_red_zone_arg2;
	unsigned long a3 = usdt_red_zone_arg3;

	USDT(optimized_attach, usdt_red_zone, a1, a2, a3);
}

#endif
