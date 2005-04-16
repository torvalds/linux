/*
 * linux/include/asm-arm/arch-l7200/system.h
 *
 * Copyright (c) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog
 *  03-21-2000  SJH	Created
 *  04-26-2000  SJH	Fixed functions
 *  05-03-2000  SJH	Removed usage of obsolete 'iomd.h'
 *  05-31-2000  SJH	Properly implemented 'arch_idle'
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

static inline void arch_idle(void)
{
	*(unsigned long *)(IO_BASE + 0x50004) = 1;	/* idle mode */
}

static inline void arch_reset(char mode)
{
	if (mode == 's') {
		cpu_reset(0);
	}
}

#endif
