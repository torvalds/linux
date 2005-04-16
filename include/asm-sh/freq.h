/*
 * include/asm-sh/freq.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef __ASM_SH_FREQ_H
#define __ASM_SH_FREQ_H
#ifdef __KERNEL__

#include <asm/cpu/freq.h>

/* arch/sh/kernel/time.c */
extern void get_current_frequency_divisors(unsigned int *ifc, unsigned int *pfc, unsigned int *bfc);

extern unsigned int get_ifc_divisor(unsigned int value);
extern unsigned int get_ifc_divisor(unsigned int value);
extern unsigned int get_ifc_divisor(unsigned int value);

extern unsigned int get_ifc_value(unsigned int divisor);
extern unsigned int get_pfc_value(unsigned int divisor);
extern unsigned int get_bfc_value(unsigned int divisor);

#endif /* __KERNEL__ */
#endif /* __ASM_SH_FREQ_H */
