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

/*
 * Force the nop1,nop10 combo of the USDT probe to a spot where the nop10
 * crosses a page boundary: .balign starts the padding exactly at a page
 * start regardless of the compiler-generated prologue size, and the 4086
 * one-byte nops put the nop1 at page offset 4086, so the following nop10
 * occupies the last 9 bytes of that page and 1 byte of the next one.
 * The kernel can't optimize such nop10, so libbpf must keep the uprobe
 * on the 1-byte nop.
 */
__attribute__((noinline))
void usdt_2_cross_page(void)
{
	asm volatile (".balign 4096, 0x90\n\t.skip 4086, 0x90");
	USDT(optimized_attach, usdt_2_cross_page);
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
