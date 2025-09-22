/*	$OpenBSD: cmmu.h,v 1.32 2013/11/16 18:45:20 miod Exp $ */
/*
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef	_M88K_CMMU_H_
#define	_M88K_CMMU_H_

/*
 * Prototypes and stuff for cmmu.c.
 */
#if defined(_KERNEL) && !defined(_LOCORE)

#include <machine/mmu.h>

/* machine dependent cmmu function pointer structure */
struct cmmu_p {
	cpuid_t (*init)(void);
	void (*batc_setup)(cpuid_t, apr_t);
	void (*setup_board_config)(void);
	void (*cpu_configuration_print)(int);
	void (*shutdown)(void);

	cpuid_t (*cpu_number)(void);

	apr_t (*apr_cmode)(void);
	apr_t (*pte_cmode)(void);
	void (*set_sapr)(apr_t);
	void (*set_uapr)(apr_t);

	void (*tlb_inv_s)(cpuid_t, vaddr_t, pt_entry_t);
	void (*tlb_inv_u)(cpuid_t, vaddr_t, pt_entry_t);
	void (*tlb_inv_all)(cpuid_t);

	void (*cache_wbinv)(cpuid_t, paddr_t, psize_t);
	void (*dcache_wb)(cpuid_t, paddr_t, psize_t);
	void (*icache_inv)(cpuid_t, paddr_t, psize_t);
	void (*dma_cachectl)(paddr_t, psize_t, int);

#ifdef MULTIPROCESSOR
	void (*dma_cachectl_local)(paddr_t, psize_t, int);
	void (*initialize_cpu)(cpuid_t);
#endif
};

extern const struct cmmu_p *cmmu;

#ifdef MULTIPROCESSOR
/*
 * On 8820x-based systems, this lock protects the CMMU SAR and SCR registers;
 * other registers may be accessed without locking it.
 * On 88410-based systems, this lock protects accesses to the BusSwitch GCSR
 * register, which masks or unmasks the 88410 control addresses.
 */
extern __cpu_simple_lock_t cmmu_cpu_lock;
#define CMMU_LOCK   __cpu_simple_lock(&cmmu_cpu_lock)
#define CMMU_UNLOCK __cpu_simple_unlock(&cmmu_cpu_lock)
#else
#define	CMMU_LOCK	do { /* nothing */ } while (0)
#define	CMMU_UNLOCK	do { /* nothing */ } while (0)
#endif	/* MULTIPROCESSOR */

#define cmmu_init			(cmmu->init)
#define cmmu_batc_setup			(cmmu->batc_setup)
#define setup_board_config		(cmmu->setup_board_config)
#define	cpu_configuration_print(cpu)	(cmmu->cpu_configuration_print)(cpu)
#define	cmmu_shutdown			(cmmu->shutdown)
#define	cmmu_cpu_number			(cmmu->cpu_number)
#define	cmmu_apr_cmode			(cmmu->apr_cmode)
#define	cmmu_pte_cmode			(cmmu->pte_cmode)
#define	cmmu_set_sapr(apr)		(cmmu->set_sapr)(apr)
#define	cmmu_set_uapr(apr)		(cmmu->set_uapr)(apr)
#define	cmmu_tlbis(cpu, va, pte) 	(cmmu->tlb_inv_s)(cpu, va, pte)
#define	cmmu_tlbiu(cpu, va, pte) 	(cmmu->tlb_inv_u)(cpu, va, pte)
#define	cmmu_tlbia(cpu) 		(cmmu->tlb_inv_all)(cpu)
#define	cmmu_cache_wbinv(cpu, pa, s)	(cmmu->cache_wbinv)(cpu, pa, s)
#define	cmmu_dcache_wb(cpu, pa, s)	(cmmu->dcache_wb)(cpu, pa, s)
#define	cmmu_icache_inv(cpu,pa,s)	(cmmu->icache_inv)(cpu, pa, s)
#define	dma_cachectl(pa, s, op)		(cmmu->dma_cachectl)(pa, s, op)
#define	dma_cachectl_local(pa, s, op)	(cmmu->dma_cachectl_local)(pa, s, op)
#define	cmmu_initialize_cpu(cpu)	(cmmu->initialize_cpu)(cpu)

/*
 * dma_cachectl{,_local}() modes
 */
#define DMA_CACHE_INV		0x00
#define DMA_CACHE_SYNC_INVAL	0x01
#define DMA_CACHE_SYNC		0x02

/*
 * Current BATC values.
 */

extern batc_t global_dbatc[BATC_MAX], global_ibatc[BATC_MAX];

#endif	/* _KERNEL && !_LOCORE */

#endif	/* _M88K_CMMU_H_ */
