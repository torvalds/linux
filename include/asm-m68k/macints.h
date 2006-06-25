/*
** macints.h -- Macintosh Linux interrupt handling structs and prototypes
**
** Copyright 1997 by Michael Schmitz
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef _ASM_MACINTS_H_
#define _ASM_MACINTS_H_

#include <asm/irq.h>

/* Setting this prints debugging info for unclaimed interrupts */

#define DEBUG_SPURIOUS

/* Setting this prints debugging info on each autovector interrupt */

/* #define DEBUG_IRQS */

/* Setting this prints debugging info on each Nubus interrupt */

/* #define DEBUG_NUBUS_INT */

/* Setting this prints debugging info on irqs as they enabled and disabled. */

/* #define DEBUG_IRQUSE */

/*
 * Base IRQ number for all Mac68K interrupt sources. Each source
 * has eight indexes (base -> base+7).
 */

#define VIA1_SOURCE_BASE	8
#define VIA2_SOURCE_BASE	16
#define MAC_SCC_SOURCE_BASE	24
#define PSC3_SOURCE_BASE	24
#define PSC4_SOURCE_BASE	32
#define PSC5_SOURCE_BASE	40
#define PSC6_SOURCE_BASE	48
#define NUBUS_SOURCE_BASE	56
#define BABOON_SOURCE_BASE	64

/*
 * Maximum IRQ number is BABOON_SOURCE_BASE + 7,
 * giving us IRQs up through 71
 */

#define NUM_MAC_SOURCES		72

/*
 * clean way to separate IRQ into its source and index
 */

#define IRQ_SRC(irq)	(irq >> 3)
#define	IRQ_IDX(irq)	(irq & 7)

/* VIA1 interrupts */
#define IRQ_VIA1_0	  (8)		/* one second int. */
#define IRQ_VIA1_1        (9)		/* VBlank int. */
#define IRQ_MAC_VBL	  IRQ_VIA1_1
#define IRQ_VIA1_2	  (10)		/* ADB SR shifts complete */
#define IRQ_MAC_ADB	  IRQ_VIA1_2
#define IRQ_MAC_ADB_SR	  IRQ_VIA1_2
#define IRQ_VIA1_3	  (11)		/* ADB SR CB2 ?? */
#define IRQ_MAC_ADB_SD	  IRQ_VIA1_3
#define IRQ_VIA1_4        (12)		/* ADB SR ext. clock pulse */
#define IRQ_MAC_ADB_CL	  IRQ_VIA1_4
#define IRQ_VIA1_5	  (13)
#define IRQ_MAC_TIMER_2	  IRQ_VIA1_5
#define IRQ_VIA1_6	  (14)
#define IRQ_MAC_TIMER_1	  IRQ_VIA1_6
#define IRQ_VIA1_7        (15)

/* VIA2/RBV interrupts */
#define IRQ_VIA2_0	  (16)
#define IRQ_MAC_SCSIDRQ	  IRQ_VIA2_0
#define IRQ_VIA2_1        (17)
#define IRQ_MAC_NUBUS	  IRQ_VIA2_1
#define IRQ_VIA2_2	  (18)
#define IRQ_VIA2_3	  (19)
#define IRQ_MAC_SCSI	  IRQ_VIA2_3
#define IRQ_VIA2_4        (20)
#define IRQ_VIA2_5	  (21)
#define IRQ_VIA2_6	  (22)
#define IRQ_VIA2_7        (23)

/* Level 3 (PSC, AV Macs only) interrupts */
#define IRQ_PSC3_0	  (24)
#define IRQ_MAC_MACE	  IRQ_PSC3_0
#define IRQ_PSC3_1	  (25)
#define IRQ_PSC3_2	  (26)
#define IRQ_PSC3_3	  (27)

/* Level 4 (SCC) interrupts */
#define IRQ_SCC		     (32)
#define IRQ_SCCA	     (33)
#define IRQ_SCCB	     (34)
#if 0 /* FIXME: are there multiple interrupt conditions on the SCC ?? */
/* SCC interrupts */
#define IRQ_SCCB_TX	     (32)
#define IRQ_SCCB_STAT	     (33)
#define IRQ_SCCB_RX	     (34)
#define IRQ_SCCB_SPCOND	     (35)
#define IRQ_SCCA_TX	     (36)
#define IRQ_SCCA_STAT	     (37)
#define IRQ_SCCA_RX	     (38)
#define IRQ_SCCA_SPCOND	     (39)
#endif

/* Level 4 (PSC, AV Macs only) interrupts */
#define IRQ_PSC4_0	  (32)
#define IRQ_PSC4_1	  (33)
#define IRQ_PSC4_2	  (34)
#define IRQ_PSC4_3	  (35)
#define IRQ_MAC_MACE_DMA  IRQ_PSC4_3

/* Level 5 (PSC, AV Macs only) interrupts */
#define IRQ_PSC5_0	  (40)
#define IRQ_PSC5_1	  (41)
#define IRQ_PSC5_2	  (42)
#define IRQ_PSC5_3	  (43)

/* Level 6 (PSC, AV Macs only) interrupts */
#define IRQ_PSC6_0	  (48)
#define IRQ_PSC6_1	  (49)
#define IRQ_PSC6_2	  (50)
#define IRQ_PSC6_3	  (51)

/* Nubus interrupts (cascaded to VIA2) */
#define IRQ_NUBUS_9	  (56)
#define IRQ_NUBUS_A	  (57)
#define IRQ_NUBUS_B	  (58)
#define IRQ_NUBUS_C	  (59)
#define IRQ_NUBUS_D	  (60)
#define IRQ_NUBUS_E	  (61)
#define IRQ_NUBUS_F	  (62)

/* Baboon interrupts (cascaded to nubus slot $C) */
#define IRQ_BABOON_0	  (64)
#define IRQ_BABOON_1	  (65)
#define IRQ_BABOON_2	  (66)
#define IRQ_BABOON_3	  (67)

#define SLOT2IRQ(x)	  (x + 47)
#define IRQ2SLOT(x)	  (x - 47)

#define INT_CLK   24576	    /* CLK while int_clk =2.456MHz and divide = 100 */
#define INT_TICKS 246	    /* to make sched_time = 99.902... HZ */

extern irq_node_t *mac_irq_list[NUM_MAC_SOURCES];
extern void mac_do_irq_list(int irq, struct pt_regs *);

#endif /* asm/macints.h */
