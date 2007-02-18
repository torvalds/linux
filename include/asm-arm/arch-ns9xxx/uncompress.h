/*
 * include/asm-arm/arch-ns9xxx/uncompress.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

static void putc(char c)
{
	volatile u8 *base = (volatile u8 *)0x40000000;
	int t = 0x10000;

	do {
		if (base[5] & 0x20) {
			base[0] = c;
			break;
		}
	} while (--t);
}

#define arch_decomp_setup()
#define arch_decomp_wdog()

static void flush(void)
{
	/* nothing */
}

#endif /* ifndef __ASM_ARCH_UNCOMPRESS_H */
