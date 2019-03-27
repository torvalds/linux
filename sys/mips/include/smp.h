/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 *	from: src/sys/alpha/include/smp.h,v 1.8 2005/01/05 20:05:50 imp
 *	JNPR: smp.h,v 1.3 2006/12/02 09:53:41 katta
 * $FreeBSD$
 *
 */

#ifndef _MACHINE_SMP_H_
#define	_MACHINE_SMP_H_

#ifdef _KERNEL

#include <sys/_cpuset.h>

#include <machine/pcb.h>

#ifdef INTRNG
# define MIPS_IPI_COUNT 	1
# define INTR_IPI_COUNT 	MIPS_IPI_COUNT
#endif

/*
 * Interprocessor interrupts for SMP.
 */
#define	IPI_RENDEZVOUS		0x0002
#define	IPI_AST			0x0004
#define	IPI_STOP		0x0008
#define	IPI_STOP_HARD		0x0008
#define	IPI_PREEMPT		0x0010
#define	IPI_HARDCLOCK		0x0020

#ifndef LOCORE

void	ipi_all_but_self(int ipi);
void	ipi_cpu(int cpu, u_int ipi);
void	ipi_selected(cpuset_t cpus, int ipi);
void	smp_init_secondary(u_int32_t cpuid);
void	mpentry(void);

extern struct pcb stoppcbs[];

#endif /* !LOCORE */
#endif /* _KERNEL */

#endif /* _MACHINE_SMP_H_ */
