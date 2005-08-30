/*
 * pmc.h
 * Copyright (C) 2004  David Gibson, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _PPC64_PMC_H
#define _PPC64_PMC_H

#include <asm/ptrace.h>

typedef void (*perf_irq_t)(struct pt_regs *);

int reserve_pmc_hardware(perf_irq_t new_perf_irq);
void release_pmc_hardware(void);

void power4_enable_pmcs(void);

#endif /* _PPC64_PMC_H */
