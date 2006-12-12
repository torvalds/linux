/*
 * include/asm-arm/arch-at91rm9200/cpu.h
 *
 *  Copyright (C) 2006 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_CPU_H
#define __ASM_ARCH_CPU_H

#include <asm/hardware.h>
#include <asm/arch/at91_dbgu.h>


#define ARCH_ID_AT91RM9200	0x09290780
#define ARCH_ID_AT91SAM9260	0x019803a0
#define ARCH_ID_AT91SAM9261	0x019703a0


static inline unsigned long at91_cpu_identify(void)
{
	return (at91_sys_read(AT91_DBGU_CIDR) & ~AT91_CIDR_VERSION);
}


#ifdef CONFIG_ARCH_AT91RM9200
#define cpu_is_at91rm9200()	(at91_cpu_identify() == ARCH_ID_AT91RM9200)
#else
#define cpu_is_at91rm9200()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9260
#define cpu_is_at91sam9260()	(at91_cpu_identify() == ARCH_ID_AT91SAM9260)
#else
#define cpu_is_at91sam9260()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9261
#define cpu_is_at91sam9261()	(at91_cpu_identify() == ARCH_ID_AT91SAM9261)
#else
#define cpu_is_at91sam9261()	(0)
#endif

#endif
