/*
** amigaints.h -- Amiga Linux interrupt handling structs and prototypes
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created 10/2/92 by Greg Harp
*/

#ifdef __KERNEL__
#ifndef _ASMm68k_AMIGAINTS_H_
#define _ASMm68k_AMIGAINTS_H_

/*
** Amiga Interrupt sources.
**
*/

#define AUTO_IRQS           (8)
#define AMI_STD_IRQS        (14)
#define CIA_IRQS            (5)
#define AMI_IRQS            (32) /* AUTO_IRQS+AMI_STD_IRQS+2*CIA_IRQS */

/* vertical blanking interrupt */
#define IRQ_AMIGA_VERTB     0

/* copper interrupt */
#define IRQ_AMIGA_COPPER    1

/* Audio interrupts */
#define IRQ_AMIGA_AUD0	    2
#define IRQ_AMIGA_AUD1	    3
#define IRQ_AMIGA_AUD2	    4
#define IRQ_AMIGA_AUD3	    5

/* Blitter done interrupt */
#define IRQ_AMIGA_BLIT	    6

/* floppy disk interrupts */
#define IRQ_AMIGA_DSKSYN    7
#define IRQ_AMIGA_DSKBLK    8

/* builtin serial port interrupts */
#define IRQ_AMIGA_RBF	    9
#define IRQ_AMIGA_TBE	    10

/* software interrupts */
#define IRQ_AMIGA_SOFT      11

/* interrupts from external hardware */
#define IRQ_AMIGA_PORTS	    12
#define IRQ_AMIGA_EXTER	    13

/* CIA interrupt sources */
#define IRQ_AMIGA_CIAA      14
#define IRQ_AMIGA_CIAA_TA   14
#define IRQ_AMIGA_CIAA_TB   15
#define IRQ_AMIGA_CIAA_ALRM 16
#define IRQ_AMIGA_CIAA_SP   17
#define IRQ_AMIGA_CIAA_FLG  18
#define IRQ_AMIGA_CIAB      19
#define IRQ_AMIGA_CIAB_TA   19
#define IRQ_AMIGA_CIAB_TB   20
#define IRQ_AMIGA_CIAB_ALRM 21
#define IRQ_AMIGA_CIAB_SP   22
#define IRQ_AMIGA_CIAB_FLG  23

/* auto-vector interrupts */
#define IRQ_AMIGA_AUTO      24
#define IRQ_AMIGA_AUTO_0    24 /* This is just a dummy */
#define IRQ_AMIGA_AUTO_1    25
#define IRQ_AMIGA_AUTO_2    26
#define IRQ_AMIGA_AUTO_3    27
#define IRQ_AMIGA_AUTO_4    28
#define IRQ_AMIGA_AUTO_5    29
#define IRQ_AMIGA_AUTO_6    30
#define IRQ_AMIGA_AUTO_7    31

#define IRQ_FLOPPY	    IRQ_AMIGA_DSKBLK

/* INTREQR masks */
#define IRQ1_MASK   0x0007	/* INTREQR mask for IRQ 1 */
#define IRQ2_MASK   0x0008	/* INTREQR mask for IRQ 2 */
#define IRQ3_MASK   0x0070	/* INTREQR mask for IRQ 3 */
#define IRQ4_MASK   0x0780	/* INTREQR mask for IRQ 4 */
#define IRQ5_MASK   0x1800	/* INTREQR mask for IRQ 5 */
#define IRQ6_MASK   0x2000	/* INTREQR mask for IRQ 6 */
#define IRQ7_MASK   0x4000	/* INTREQR mask for IRQ 7 */

#define IF_SETCLR   0x8000      /* set/clr bit */
#define IF_INTEN    0x4000	/* master interrupt bit in INT* registers */
#define IF_EXTER    0x2000	/* external level 6 and CIA B interrupt */
#define IF_DSKSYN   0x1000	/* disk sync interrupt */
#define IF_RBF	    0x0800	/* serial receive buffer full interrupt */
#define IF_AUD3     0x0400	/* audio channel 3 done interrupt */
#define IF_AUD2     0x0200	/* audio channel 2 done interrupt */
#define IF_AUD1     0x0100	/* audio channel 1 done interrupt */
#define IF_AUD0     0x0080	/* audio channel 0 done interrupt */
#define IF_BLIT     0x0040	/* blitter done interrupt */
#define IF_VERTB    0x0020	/* vertical blanking interrupt */
#define IF_COPER    0x0010	/* copper interrupt */
#define IF_PORTS    0x0008	/* external level 2 and CIA A interrupt */
#define IF_SOFT     0x0004	/* software initiated interrupt */
#define IF_DSKBLK   0x0002	/* diskblock DMA finished */
#define IF_TBE	    0x0001	/* serial transmit buffer empty interrupt */

extern void amiga_do_irq(int irq, struct pt_regs *fp);
extern void amiga_do_irq_list(int irq, struct pt_regs *fp);

/* CIA interrupt control register bits */

#define CIA_ICR_TA	0x01
#define CIA_ICR_TB	0x02
#define CIA_ICR_ALRM	0x04
#define CIA_ICR_SP	0x08
#define CIA_ICR_FLG	0x10
#define CIA_ICR_ALL	0x1f
#define CIA_ICR_SETCLR	0x80

/* to access the interrupt control registers of CIA's use only
** these functions, they behave exactly like the amiga os routines
*/

extern struct ciabase ciaa_base, ciab_base;

extern unsigned char cia_set_irq(unsigned int irq, int set);
extern unsigned char cia_able_irq(unsigned int irq, int enable);

#endif /* asm-m68k/amigaints.h */
#endif /* __KERNEL__ */
