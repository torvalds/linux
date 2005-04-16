/*
 *  linux/include/asm-arm/arch-ebsa110/uncompress.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	unsigned long tmp1, tmp2;
	__asm__ __volatile__(
	"ldrb	%0, [%2], #1\n"
"	teq	%0, #0\n"
"	beq	3f\n"
"1:	strb	%0, [%3]\n"
"2:	ldrb	%1, [%3, #0x14]\n"
"	and	%1, %1, #0x60\n"
"	teq	%1, #0x60\n"
"	bne	2b\n"
"	teq	%0, #'\n'\n"
"	moveq	%0, #'\r'\n"
"	beq	1b\n"
"	ldrb	%0, [%2], #1\n"
"	teq	%0, #0\n"
"	bne	1b\n"
"3:	ldrb	%1, [%3, #0x14]\n"
"	and	%1, %1, #0x60\n"
"	teq	%1, #0x60\n"
"	bne	3b"
	: "=&r" (tmp1), "=&r" (tmp2)
	: "r" (s), "r" (0xf0000be0) : "cc");
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
