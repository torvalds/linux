/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 *
 */

#ifndef _MACHINE_SMP_H_
#define _MACHINE_SMP_H_

#ifdef _KERNEL

#ifdef SMP

#ifndef LOCORE

#include <x86/x86_smp.h>

/* global symbols in mpboot.S */
extern char			mptramp_start[];
extern u_int32_t		mptramp_pagetables;

/* IPI handlers */
inthand_t
	IDTVEC(justreturn),	/* interrupt CPU with minimum overhead */
	IDTVEC(justreturn1_pti),
	IDTVEC(invltlb_pti),
	IDTVEC(invltlb_pcid_pti),
	IDTVEC(invltlb_pcid),	/* TLB shootdowns - global, pcid */
	IDTVEC(invltlb_invpcid_pti_pti),
	IDTVEC(invltlb_invpcid_nopti),
	IDTVEC(invlpg_pti),
	IDTVEC(invlpg_invpcid_pti),
	IDTVEC(invlpg_invpcid),
	IDTVEC(invlpg_pcid_pti),
	IDTVEC(invlpg_pcid),
	IDTVEC(invlrng_pti),
	IDTVEC(invlrng_invpcid_pti),
	IDTVEC(invlrng_invpcid),
	IDTVEC(invlrng_pcid_pti),
	IDTVEC(invlrng_pcid),
	IDTVEC(invlcache_pti),
	IDTVEC(ipi_intr_bitmap_handler_pti),
	IDTVEC(cpustop_pti),
	IDTVEC(cpususpend_pti),
	IDTVEC(rendezvous_pti);

void	invltlb_pcid_handler(void);
void	invltlb_invpcid_handler(void);
void	invltlb_invpcid_pti_handler(void);
void	invlpg_invpcid_handler(void);
void	invlpg_pcid_handler(void);
void	invlrng_invpcid_handler(void);
void	invlrng_pcid_handler(void);
int	native_start_all_aps(void);
void	mp_bootaddress(vm_paddr_t *, unsigned int *);

#endif /* !LOCORE */
#endif /* SMP */

#endif /* _KERNEL */
#endif /* _MACHINE_SMP_H_ */
