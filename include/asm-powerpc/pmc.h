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
#ifndef _POWERPC_PMC_H
#define _POWERPC_PMC_H

#include <asm/ptrace.h>

typedef void (*perf_irq_t)(struct pt_regs *);
extern perf_irq_t perf_irq;

int reserve_pmc_hardware(perf_irq_t new_perf_irq);
void release_pmc_hardware(void);

#ifdef CONFIG_PPC64
void power4_enable_pmcs(void);
#endif

#ifdef CONFIG_FSL_BOOKE
void init_pmc_stop(int ctr);
void set_pmc_event(int ctr, int event);
void set_pmc_user_kernel(int ctr, int user, int kernel);
void set_pmc_marked(int ctr, int mark0, int mark1);
void pmc_start_ctr(int ctr, int enable);
void pmc_start_ctrs(int enable);
void pmc_stop_ctrs(void);
void dump_pmcs(void);

extern struct op_powerpc_model op_model_fsl_booke;
#endif

#endif /* _POWERPC_PMC_H */
