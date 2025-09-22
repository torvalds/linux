/*	$OpenBSD: intrdefs.h,v 1.20 2025/06/11 09:57:01 kettenis Exp $	*/
/*	$NetBSD: intrdefs.h,v 1.2 2003/05/04 22:01:56 fvdl Exp $	*/

#ifndef _I386_INTRDEFS_H
#define _I386_INTRDEFS_H

/*
 * Intel APICs (advanced programmable interrupt controllers) have
 * bytesized priority registers where the upper nibble is the actual
 * interrupt priority level (a.k.a. IPL).  Interrupt vectors are
 * closely tied to these levels as interrupts whose vectors' upper
 * nibble is lower than or equal to the current level are blocked.
 * Not all 256 possible vectors are available for interrupts in
 * APIC systems, only
 *
 * For systems where instead the older ICU (interrupt controlling
 * unit, a.k.a. PIC or 82C59) is used, the IPL is not directly useful,
 * since the interrupt blocking is handled via interrupt masks instead
 * of levels.  However the IPL is easily used as an offset into arrays
 * of masks.
 */
#define IPLSHIFT 4	/* The upper nibble of vectors is the IPL.	*/
#define NIPL 16		/* Four bits of information gives as much.	*/
#define IPL(level) ((level) >> IPLSHIFT)	/* Extract the IPL.	*/
/* XXX Maybe this IDTVECOFF definition should be elsewhere? */
#define IDTVECOFF 0x20	/* The lower 32 IDT vectors are reserved.	*/

/*
 * This macro is only defined for 0 <= x < 14, i.e. there are fourteen
 * distinct priority levels available for interrupts.
 */
#define MAKEIPL(priority) (IDTVECOFF + ((priority) << IPLSHIFT))

/*
 * Interrupt priority levels.
 *
 * XXX We are somewhat sloppy about what we mean by IPLs, sometimes
 * XXX we refer to the eight-bit value suitable for storing into APICs'
 * XXX priority registers, other times about the four-bit entity found
 * XXX in the former values' upper nibble, which can be used as offsets
 * XXX in various arrays of our implementation.  We are hoping that
 * XXX the context will provide enough information to not make this
 * XXX sloppy naming a real problem.
 * 
 * There are tty, network and disk drivers that use free() at interrupt
 * time, so imp > (tty | net | bio).
 *
 * Since run queues may be manipulated by both the statclock and tty,
 * network, and disk drivers, clock > imp.
 *
 * IPL_HIGH must block everything that can manipulate a run queue.
 *
 * XXX Ultimately we may need serial drivers to run at the absolute highest
 * XXX priority to avoid overruns, then we must make serial > high.
 *
 * The level numbers are picked to fit into APIC vector priorities.
 */
#define	IPL_NONE	0		/* nothing */
#define	IPL_SOFTCLOCK	MAKEIPL(1)	/* timeouts */
#define	IPL_SOFTNET	MAKEIPL(2)	/* protocol stacks */
#define	IPL_BIO		MAKEIPL(3)	/* block I/O */
#define	IPL_NET		MAKEIPL(4)	/* network */
#define	IPL_SOFTTTY	MAKEIPL(5)	/* delayed terminal handling */
#define	IPL_TTY		MAKEIPL(6)	/* terminal */
#define	IPL_VM		MAKEIPL(7)	/* memory allocation */
#define	IPL_AUDIO	MAKEIPL(8)	/* audio */
#define	IPL_CLOCK	MAKEIPL(9)	/* clock */
#define	IPL_STATCLOCK	IPL_CLOCK	/* statclock */
#define	IPL_SCHED	IPL_CLOCK
#define	IPL_HIGH	MAKEIPL(10)	/* everything */
#define	IPL_IPI		MAKEIPL(11)	/* interprocessor interrupt */

#define	IPL_MPFLOOR	IPL_TTY
#define	IPL_MPSAFE	0x100
#define	IPL_WAKEUP	0

/* Interrupt sharing types. */
#define	IST_NONE	0	/* none */
#define	IST_PULSE	1	/* pulsed */
#define	IST_EDGE	2	/* edge-triggered */
#define	IST_LEVEL	3	/* level-triggered */

/*
 * Local APIC masks. Must not conflict with SIR_* below, and must
 * be >= NUM_LEGACY_IRQs. Note that LIR_IPI must be first.
 */
#define LIR_IPI		31
#define LIR_TIMER	30

/* Soft interrupt masks. */
#define	SIR_CLOCK	29
#define	SIR_NET		28
#define	SIR_TTY		27


/*
 * Maximum # of interrupt sources per CPU. 32 to fit in one word.
 * ioapics can theoretically produce more, but it's not likely to
 * happen. For multiple ioapics, things can be routed to different
 * CPUs.
 */
#define MAX_INTR_SOURCES	32
#define NUM_LEGACY_IRQS		16

/*
 * Low and high boundaries between which interrupt gates will
 * be allocated in the IDT.
 */
#define IDT_INTR_LOW	(0x20 + NUM_LEGACY_IRQS)
#define IDT_INTR_HIGH	0xef

#define I386_IPI_HALT		0x00000001
#define I386_IPI_NOP		0x00000002
#define I386_IPI_FLUSH_FPU	0x00000004
#define I386_IPI_SYNCH_FPU	0x00000008
#define I386_IPI_MTRR		0x00000010
#define I386_IPI_GDT		0x00000020
#define I386_IPI_DDB		0x00000040	/* synchronize while in ddb */
#define I386_IPI_SETPERF	0x00000080
#define I386_IPI_WBINVD		0x00000100

#define I386_NIPI	9

#define IREENT_MAGIC	0x18041969

#endif /* _I386_INTRDEFS_H */
