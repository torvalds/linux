/*
 * include/asm-arm/arch-at91/cpu.h
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
#define ARCH_ID_AT91SAM9263	0x019607a0
#define ARCH_ID_AT91SAM9RL64	0x019b03a0
#define ARCH_ID_AT91CAP9	0x039A03A0

#define ARCH_ID_AT91SAM9XE128	0x329973a0
#define ARCH_ID_AT91SAM9XE256	0x329a93a0
#define ARCH_ID_AT91SAM9XE512	0x329aa3a0

#define ARCH_ID_AT91M40800	0x14080044
#define ARCH_ID_AT91R40807	0x44080746
#define ARCH_ID_AT91M40807	0x14080745
#define ARCH_ID_AT91R40008	0x44000840

static inline unsigned long at91_cpu_identify(void)
{
	return (at91_sys_read(AT91_DBGU_CIDR) & ~AT91_CIDR_VERSION);
}


#define ARCH_FAMILY_AT91X92	0x09200000
#define ARCH_FAMILY_AT91SAM9	0x01900000
#define ARCH_FAMILY_AT91SAM9XE	0x02900000

static inline unsigned long at91_arch_identify(void)
{
	return (at91_sys_read(AT91_DBGU_CIDR) & AT91_CIDR_ARCH);
}


#ifdef CONFIG_ARCH_AT91RM9200
#define cpu_is_at91rm9200()	(at91_cpu_identify() == ARCH_ID_AT91RM9200)
#else
#define cpu_is_at91rm9200()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9260
#define cpu_is_at91sam9xe()	(at91_arch_identify() == ARCH_FAMILY_AT91SAM9XE)
#define cpu_is_at91sam9260()	((at91_cpu_identify() == ARCH_ID_AT91SAM9260) || cpu_is_at91sam9xe())
#else
#define cpu_is_at91sam9xe()	(0)
#define cpu_is_at91sam9260()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9261
#define cpu_is_at91sam9261()	(at91_cpu_identify() == ARCH_ID_AT91SAM9261)
#else
#define cpu_is_at91sam9261()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9263
#define cpu_is_at91sam9263()	(at91_cpu_identify() == ARCH_ID_AT91SAM9263)
#else
#define cpu_is_at91sam9263()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9RL
#define cpu_is_at91sam9rl()	(at91_cpu_identify() == ARCH_ID_AT91SAM9RL64)
#else
#define cpu_is_at91sam9rl()	(0)
#endif

#ifdef CONFIG_ARCH_AT91CAP9
#define cpu_is_at91cap9()	(at91_cpu_identify() == ARCH_ID_AT91CAP9)
#else
#define cpu_is_at91cap9()	(0)
#endif

/*
 * Since this is ARM, we will never run on any AVR32 CPU. But these
 * definitions may reduce clutter in common drivers.
 */
#define cpu_is_at32ap7000()	(0)

#endif
