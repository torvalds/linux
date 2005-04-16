#ifndef _ASM_IRQ_VECTORS_H
#define _ASM_IRQ_VECTORS_H

/*
 * IDT vectors usable for external interrupt sources start
 * at 0x20:
 */
#define FIRST_EXTERNAL_VECTOR	0x20

#define SYSCALL_VECTOR		0x80

/*
 * Vectors 0x20-0x2f are used for ISA interrupts.
 */

/*
 * Special IRQ vectors used by the SMP architecture, 0xf0-0xff
 *
 *  some of the following vectors are 'rare', they are merged
 *  into a single vector (CALL_FUNCTION_VECTOR) to save vector space.
 *  TLB, reschedule and local APIC vectors are performance-critical.
 *
 *  Vectors 0xf0-0xfa are free (reserved for future Linux use).
 */
#define SPURIOUS_APIC_VECTOR	0xff
#define ERROR_APIC_VECTOR	0xfe
#define INVALIDATE_TLB_VECTOR	0xfd
#define RESCHEDULE_VECTOR	0xfc
#define CALL_FUNCTION_VECTOR	0xfb

#define THERMAL_APIC_VECTOR	0xf0
/*
 * Local APIC timer IRQ vector is on a different priority level,
 * to work around the 'lost local interrupt if more than 2 IRQ
 * sources per level' errata.
 */
#define LOCAL_TIMER_VECTOR	0xef

/*
 * First APIC vector available to drivers: (vectors 0x30-0xee)
 * we start at 0x31 to spread out vectors evenly between priority
 * levels. (0x80 is the syscall vector)
 */
#define FIRST_DEVICE_VECTOR	0x31
#define FIRST_SYSTEM_VECTOR	0xef

#define TIMER_IRQ 0

/*
 * IRQ definitions
 */
#define NR_VECTORS 256
#define NR_IRQS 224
#define NR_IRQ_VECTORS NR_IRQS

#define FPU_IRQ			13

#define	FIRST_VM86_IRQ		3
#define LAST_VM86_IRQ		15
#define invalid_vm86_irq(irq)	((irq) < 3 || (irq) > 15)

#endif /* _ASM_IRQ_VECTORS_H */
