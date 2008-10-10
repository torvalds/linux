#ifndef ASM_X86__VISWS__COBALT_H
#define ASM_X86__VISWS__COBALT_H

#include <asm/fixmap.h>

/*
 * Cobalt SGI Visual Workstation system ASIC
 */ 

#define CO_CPU_NUM_PHYS 0x1e00
#define CO_CPU_TAB_PHYS (CO_CPU_NUM_PHYS + 2)

#define CO_CPU_MAX 4

#define	CO_CPU_PHYS		0xc2000000
#define	CO_APIC_PHYS		0xc4000000

/* see set_fixmap() and asm/fixmap.h */
#define	CO_CPU_VADDR		(fix_to_virt(FIX_CO_CPU))
#define	CO_APIC_VADDR		(fix_to_virt(FIX_CO_APIC))

/* Cobalt CPU registers -- relative to CO_CPU_VADDR, use co_cpu_*() */
#define	CO_CPU_REV		0x08
#define	CO_CPU_CTRL		0x10
#define	CO_CPU_STAT		0x20
#define	CO_CPU_TIMEVAL		0x30

/* CO_CPU_CTRL bits */
#define	CO_CTRL_TIMERUN		0x04		/* 0 == disabled */
#define	CO_CTRL_TIMEMASK	0x08		/* 0 == unmasked */

/* CO_CPU_STATUS bits */
#define	CO_STAT_TIMEINTR	0x02	/* (r) 1 == int pend, (w) 0 == clear */

/* CO_CPU_TIMEVAL value */
#define	CO_TIME_HZ		100000000	/* Cobalt core rate */

/* Cobalt APIC registers -- relative to CO_APIC_VADDR, use co_apic_*() */
#define	CO_APIC_HI(n)		(((n) * 0x10) + 4)
#define	CO_APIC_LO(n)		((n) * 0x10)
#define	CO_APIC_ID		0x0ffc

/* CO_APIC_ID bits */
#define	CO_APIC_ENABLE		0x00000100

/* CO_APIC_LO bits */
#define	CO_APIC_MASK		0x00010000	/* 0 = enabled */
#define	CO_APIC_LEVEL		0x00008000	/* 0 = edge */

/*
 * Where things are physically wired to Cobalt
 * #defines with no board _<type>_<rev>_ are common to all (thus far)
 */
#define	CO_APIC_IDE0		4
#define CO_APIC_IDE1		2		/* Only on 320 */

#define	CO_APIC_8259		12		/* serial, floppy, par-l-l */

/* Lithium PCI Bridge A -- "the one with 82557 Ethernet" */
#define	CO_APIC_PCIA_BASE0	0 /* and 1 */	/* slot 0, line 0 */
#define	CO_APIC_PCIA_BASE123	5 /* and 6 */	/* slot 0, line 1 */

#define	CO_APIC_PIIX4_USB	7		/* this one is weird */

/* Lithium PCI Bridge B -- "the one with PIIX4" */
#define	CO_APIC_PCIB_BASE0	8 /* and 9-12 *//* slot 0, line 0 */
#define	CO_APIC_PCIB_BASE123	13 /* 14.15 */	/* slot 0, line 1 */

#define	CO_APIC_VIDOUT0		16
#define	CO_APIC_VIDOUT1		17
#define	CO_APIC_VIDIN0		18
#define	CO_APIC_VIDIN1		19

#define	CO_APIC_LI_AUDIO	22

#define	CO_APIC_AS		24
#define	CO_APIC_RE		25

#define CO_APIC_CPU		28		/* Timer and Cache interrupt */
#define	CO_APIC_NMI		29
#define	CO_APIC_LAST		CO_APIC_NMI

/*
 * This is how irqs are assigned on the Visual Workstation.
 * Legacy devices get irq's 1-15 (system clock is 0 and is CO_APIC_CPU).
 * All other devices (including PCI) go to Cobalt and are irq's 16 on up.
 */
#define	CO_IRQ_APIC0	16			/* irq of apic entry 0 */
#define	IS_CO_APIC(irq)	((irq) >= CO_IRQ_APIC0)
#define	CO_IRQ(apic)	(CO_IRQ_APIC0 + (apic))	/* apic ent to irq */
#define	CO_APIC(irq)	((irq) - CO_IRQ_APIC0)	/* irq to apic ent */
#define CO_IRQ_IDE0	14			/* knowledge of... */
#define CO_IRQ_IDE1	15			/* ... ide driver defaults! */
#define	CO_IRQ_8259	CO_IRQ(CO_APIC_8259)

#ifdef CONFIG_X86_VISWS_APIC
static inline void co_cpu_write(unsigned long reg, unsigned long v)
{
	*((volatile unsigned long *)(CO_CPU_VADDR+reg))=v;
}

static inline unsigned long co_cpu_read(unsigned long reg)
{
	return *((volatile unsigned long *)(CO_CPU_VADDR+reg));
}            
             
static inline void co_apic_write(unsigned long reg, unsigned long v)
{
	*((volatile unsigned long *)(CO_APIC_VADDR+reg))=v;
}            
             
static inline unsigned long co_apic_read(unsigned long reg)
{
	return *((volatile unsigned long *)(CO_APIC_VADDR+reg));
}
#endif

extern char visws_board_type;

#define	VISWS_320	0
#define	VISWS_540	1

extern char visws_board_rev;

#endif /* ASM_X86__VISWS__COBALT_H */
