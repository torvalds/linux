/*-
 * SPDX-License-Identifier: Beerware
 *
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

#ifndef _X86_X86_SMP_H_
#define	_X86_X86_SMP_H_

#include <sys/bus.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <machine/pcb.h>

struct pmap;

/* global data in mp_x86.c */
extern int mp_naps;
extern int boot_cpu_id;
extern struct pcb stoppcbs[];
extern int cpu_apic_ids[];
extern int bootAP;
extern void *dpcpu;
extern char *bootSTK;
extern void *bootstacks[];
extern unsigned int boot_address;
extern unsigned int bootMP_size;
extern volatile u_int cpu_ipi_pending[];
extern volatile int aps_ready;
extern struct mtx ap_boot_mtx;
extern int cpu_logical;
extern int cpu_cores;
extern volatile uint32_t smp_tlb_generation;
extern struct pmap *smp_tlb_pmap;
extern vm_offset_t smp_tlb_addr1, smp_tlb_addr2;
extern u_int xhits_gbl[];
extern u_int xhits_pg[];
extern u_int xhits_rng[];
extern u_int ipi_global;
extern u_int ipi_page;
extern u_int ipi_range;
extern u_int ipi_range_size;

extern int nmi_kdb_lock;
extern int nmi_is_broadcast;

struct cpu_info {
	int	cpu_present:1;
	int	cpu_bsp:1;
	int	cpu_disabled:1;
	int	cpu_hyperthread:1;
};
extern struct cpu_info *cpu_info;

#ifdef COUNT_IPIS
extern u_long *ipi_invltlb_counts[MAXCPU];
extern u_long *ipi_invlrng_counts[MAXCPU];
extern u_long *ipi_invlpg_counts[MAXCPU];
extern u_long *ipi_invlcache_counts[MAXCPU];
extern u_long *ipi_rendezvous_counts[MAXCPU];
#endif

/* IPI handlers */
inthand_t
	IDTVEC(invltlb),	/* TLB shootdowns - global */
	IDTVEC(invlpg),		/* TLB shootdowns - 1 page */
	IDTVEC(invlrng),	/* TLB shootdowns - page range */
	IDTVEC(invlcache),	/* Write back and invalidate cache */
	IDTVEC(ipi_intr_bitmap_handler), /* Bitmap based IPIs */ 
	IDTVEC(cpustop),	/* CPU stops & waits to be restarted */
	IDTVEC(cpususpend),	/* CPU suspends & waits to be resumed */
	IDTVEC(rendezvous);	/* handle CPU rendezvous */

/* functions in x86_mp.c */
void	assign_cpu_ids(void);
void	cpu_add(u_int apic_id, char boot_cpu);
void	cpustop_handler(void);
void	cpususpend_handler(void);
void	alloc_ap_trampoline(vm_paddr_t *physmap, unsigned int *physmap_idx);
void	init_secondary_tail(void);
void	invltlb_handler(void);
void	invlpg_handler(void);
void	invlrng_handler(void);
void	invlcache_handler(void);
void	init_secondary(void);
void	ipi_startup(int apic_id, int vector);
void	ipi_all_but_self(u_int ipi);
void 	ipi_bitmap_handler(struct trapframe frame);
void	ipi_cpu(int cpu, u_int ipi);
int	ipi_nmi_handler(void);
void	ipi_selected(cpuset_t cpus, u_int ipi);
void	set_interrupt_apic_ids(void);
void	smp_cache_flush(void);
void	smp_masked_invlpg(cpuset_t mask, vm_offset_t addr, struct pmap *pmap);
void	smp_masked_invlpg_range(cpuset_t mask, vm_offset_t startva,
	    vm_offset_t endva, struct pmap *pmap);
void	smp_masked_invltlb(cpuset_t mask, struct pmap *pmap);
void	mem_range_AP_init(void);
void	topo_probe(void);
void	ipi_send_cpu(int cpu, u_int ipi);

#endif
