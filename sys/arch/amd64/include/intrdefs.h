/*	$OpenBSD: intrdefs.h,v 1.24 2024/05/26 13:37:31 kettenis Exp $	*/
/*	$NetBSD: intrdefs.h,v 1.2 2003/05/04 22:01:56 fvdl Exp $	*/

#ifndef _AMD64_INTRDEFS_H
#define _AMD64_INTRDEFS_H

/*
 * Interrupt priority levels.
 * 
 * There are tty, network and disk drivers that use free() at interrupt
 * time, so imp > (tty | net | bio).
 *
 * Since run queues may be manipulated by both the statclock and tty,
 * network, and disk drivers, clock > imp.
 *
 * IPL_HIGH must block everything that can manipulate a run queue.
 *
 * The level numbers are picked to fit into APIC vector priorities.
 *
 */
#define	IPL_NONE	0x0	/* nothing */
#define	IPL_SOFTCLOCK	0x1	/* timeouts */
#define	IPL_SOFTNET	0x2	/* protocol stacks */
#define	IPL_BIO		0x3	/* block I/O */
#define	IPL_NET		0x4	/* network */
#define	IPL_SOFTTTY	0x8	/* delayed terminal handling */
#define	IPL_TTY		0x9	/* terminal */
#define	IPL_VM		0xa	/* memory allocation */
#define	IPL_AUDIO	0xb	/* audio */
#define	IPL_CLOCK	0xc	/* clock */
#define	IPL_SCHED	IPL_CLOCK
#define	IPL_STATCLOCK	IPL_CLOCK
#define	IPL_HIGH	0xd	/* everything */
#define	IPL_IPI		0xe	/* inter-processor interrupts */
#define	NIPL		16

#define	IPL_MPFLOOR	IPL_TTY
#define	IPL_MPSAFE	0x100
#define	IPL_WAKEUP	0x200

/* Interrupt sharing types. */
#define	IST_NONE	0	/* none */
#define	IST_PULSE	1	/* pulsed */
#define	IST_EDGE	2	/* edge-triggered */
#define	IST_LEVEL	3	/* level-triggered */

/*
 * Local APIC masks. Must not conflict with SIR_* above, and must
 * be >= NUM_LEGACY_IRQs. Note that LIR_IPI must be first.
 */
#define LIR_IPI		63
#define LIR_TIMER	62

/* Soft interrupt masks. */
#define	SIR_CLOCK	61
#define	SIR_NET		60
#define	SIR_TTY		59

#define	LIR_XEN		58
#define	LIR_HYPERV	57

/*
 * Maximum # of interrupt sources per CPU. 64 to fit in one word.
 * ioapics can theoretically produce more, but it's not likely to
 * happen. For multiple ioapics, things can be routed to different
 * CPUs.
 */
#define MAX_INTR_SOURCES	64
#define NUM_LEGACY_IRQS		16

/*
 * Low and high boundaries between which interrupt gates will
 * be allocated in the IDT.
 */
#define IDT_INTR_LOW	(0x20 + NUM_LEGACY_IRQS)
#define IDT_INTR_HIGH	0xef

#define X86_IPI_HALT			0x00000001
#define X86_IPI_NOP			0x00000002
#define X86_IPI_VMCLEAR_VMM		0x00000004
#define X86_IPI_PCTR			0x00000010
#define X86_IPI_MTRR			0x00000020
#define X86_IPI_SETPERF			0x00000040
#define X86_IPI_DDB			0x00000080
#define X86_IPI_START_VMM		0x00000100
#define X86_IPI_STOP_VMM		0x00000200
#define X86_IPI_WBINVD			0x00000400

#define X86_NIPI			12

#define IREENT_MAGIC	0x18041969

#endif /* _AMD64_INTRDEFS_H */
