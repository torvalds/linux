#ifndef _ASM_HW_IRQ_H
#define _ASM_HW_IRQ_H

/*
 *	linux/include/asm/hw_irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	moved some of the old arch/i386/kernel/irq.h to here. VY
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 *
 *	hacked by Andi Kleen for x86-64.
 */

#ifndef __ASSEMBLY__
#include <asm/atomic.h>
#include <asm/irq.h>
#include <linux/profile.h>
#include <linux/smp.h>

struct hw_interrupt_type;
#endif

#define NMI_VECTOR		0x02
/*
 * IDT vectors usable for external interrupt sources start
 * at 0x20:
 */
#define FIRST_EXTERNAL_VECTOR	0x20

#define IA32_SYSCALL_VECTOR	0x80


/*
 * Vectors 0x20-0x2f are used for ISA interrupts.
 */

/*
 * Special IRQ vectors used by the SMP architecture, 0xf0-0xff
 *
 *  some of the following vectors are 'rare', they are merged
 *  into a single vector (CALL_FUNCTION_VECTOR) to save vector space.
 *  TLB, reschedule and local APIC vectors are performance-critical.
 */
#define SPURIOUS_APIC_VECTOR	0xff
#define ERROR_APIC_VECTOR	0xfe
#define RESCHEDULE_VECTOR	0xfd
#define CALL_FUNCTION_VECTOR	0xfc
/* fb free - please don't readd KDB here because it's useless
   (hint - think what a NMI bit does to a vector) */
#define THERMAL_APIC_VECTOR	0xfa
#define THRESHOLD_APIC_VECTOR   0xf9
/* f8 free */
#define INVALIDATE_TLB_VECTOR_END	0xf7
#define INVALIDATE_TLB_VECTOR_START	0xf0	/* f0-f7 used for TLB flush */

#define NUM_INVALIDATE_TLB_VECTORS	8

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
#define FIRST_SYSTEM_VECTOR	0xef   /* duplicated in irq.h */


#ifndef __ASSEMBLY__
extern u8 irq_vector[NR_IRQ_VECTORS];
#define IO_APIC_VECTOR(irq)	(irq_vector[irq])
#define AUTO_ASSIGN		-1

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

extern void disable_8259A_irq(unsigned int irq);
extern void enable_8259A_irq(unsigned int irq);
extern int i8259A_irq_pending(unsigned int irq);
extern void make_8259A_irq(unsigned int irq);
extern void init_8259A(int aeoi);
extern void FASTCALL(send_IPI_self(int vector));
extern void init_VISWS_APIC_irqs(void);
extern void setup_IO_APIC(void);
extern void disable_IO_APIC(void);
extern void print_IO_APIC(void);
extern int IO_APIC_get_PCI_irq_vector(int bus, int slot, int fn);
extern void send_IPI(int dest, int vector);
extern void setup_ioapic_dest(void);

extern unsigned long io_apic_irqs;

extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

#define IO_APIC_IRQ(x) (((x) >= 16) || ((1<<(x)) & io_apic_irqs))

#define __STR(x) #x
#define STR(x) __STR(x)

#include <asm/ptrace.h>

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

/*
 *	SMP has a few special interrupts for IPI messages
 */

#define BUILD_IRQ(nr) \
asmlinkage void IRQ_NAME(nr); \
__asm__( \
"\n.p2align\n" \
"IRQ" #nr "_interrupt:\n\t" \
	"push $~(" #nr ") ; " \
	"jmp common_interrupt");

#define platform_legacy_irq(irq)	((irq) < 16)

#endif

#endif /* _ASM_HW_IRQ_H */
