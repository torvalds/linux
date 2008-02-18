/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ASM_POWERPC_DCR_NATIVE_H
#define _ASM_POWERPC_DCR_NATIVE_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/spinlock.h>

typedef struct {
	unsigned int base;
} dcr_host_t;

#define DCR_MAP_OK(host)	(1)

#define dcr_map(dev, dcr_n, dcr_c)	((dcr_host_t){ .base = (dcr_n) })
#define dcr_unmap(host, dcr_c)		do {} while (0)
#define dcr_read(host, dcr_n)		mfdcr(dcr_n + host.base)
#define dcr_write(host, dcr_n, value)	mtdcr(dcr_n + host.base, value)

/* Device Control Registers */
void __mtdcr(int reg, unsigned int val);
unsigned int __mfdcr(int reg);
#define mfdcr(rn)						\
	({unsigned int rval;					\
	if (__builtin_constant_p(rn))				\
		asm volatile("mfdcr %0," __stringify(rn)	\
		              : "=r" (rval));			\
	else							\
		rval = __mfdcr(rn);				\
	rval;})

#define mtdcr(rn, v)						\
do {								\
	if (__builtin_constant_p(rn))				\
		asm volatile("mtdcr " __stringify(rn) ",%0"	\
			      : : "r" (v)); 			\
	else							\
		__mtdcr(rn, v);					\
} while (0)

/* R/W of indirect DCRs make use of standard naming conventions for DCRs */
extern spinlock_t dcr_ind_lock;

static inline unsigned __mfdcri(int base_addr, int base_data, int reg)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&dcr_ind_lock, flags);
	__mtdcr(base_addr, reg);
	val = __mfdcr(base_data);
	spin_unlock_irqrestore(&dcr_ind_lock, flags);
	return val;
}

static inline void __mtdcri(int base_addr, int base_data, int reg,
			    unsigned val)
{
	unsigned long flags;

	spin_lock_irqsave(&dcr_ind_lock, flags);
	__mtdcr(base_addr, reg);
	__mtdcr(base_data, val);
	spin_unlock_irqrestore(&dcr_ind_lock, flags);
}

#define mfdcri(base, reg)	__mfdcri(DCRN_ ## base ## _CONFIG_ADDR,	\
					 DCRN_ ## base ## _CONFIG_DATA,	\
					 reg)

#define mtdcri(base, reg, data)	__mtdcri(DCRN_ ## base ## _CONFIG_ADDR,	\
					 DCRN_ ## base ## _CONFIG_DATA,	\
					 reg, data)

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_NATIVE_H */
