// SPDX-License-Identifier: GPL-2.0

#if defined(__x86_64__)

/*
 * Include usdt.h with defined USDT_NOP macro to use single
 * nop instruction.
 */
#define USDT_NOP .byte 0x90
#include "usdt.h"

__attribute__((aligned(16)))
void usdt_1(void)
{
	USDT(optimized_attach, usdt_1);
}

#endif
