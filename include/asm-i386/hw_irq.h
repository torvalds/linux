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
 */

#include <linux/profile.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/sections.h>

#define NMI_VECTOR		0x02

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

extern void (*interrupt[NR_IRQS])(void);

#ifdef CONFIG_SMP
fastcall void reschedule_interrupt(void);
fastcall void invalidate_interrupt(void);
fastcall void call_function_interrupt(void);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
fastcall void apic_timer_interrupt(void);
fastcall void error_interrupt(void);
fastcall void spurious_interrupt(void);
fastcall void thermal_interrupt(void);
#define platform_legacy_irq(irq)	((irq) < 16)
#endif

void disable_8259A_irq(unsigned int irq);
void enable_8259A_irq(unsigned int irq);
int i8259A_irq_pending(unsigned int irq);
void make_8259A_irq(unsigned int irq);
void init_8259A(int aeoi);
void FASTCALL(send_IPI_self(int vector));
void init_VISWS_APIC_irqs(void);
void setup_IO_APIC(void);
void disable_IO_APIC(void);
void print_IO_APIC(void);
int IO_APIC_get_PCI_irq_vector(int bus, int slot, int fn);
void send_IPI(int dest, int vector);
void setup_ioapic_dest(void);

extern unsigned long io_apic_irqs;

extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

#define IO_APIC_IRQ(x) (((x) >= 16) || ((1<<(x)) & io_apic_irqs))

#endif /* _ASM_HW_IRQ_H */
