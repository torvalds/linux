/*
 *  include/asm-ppc/open_pic.h -- OpenPIC Interrupt Handling
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 */

#ifndef _PPC_KERNEL_OPEN_PIC_H
#define _PPC_KERNEL_OPEN_PIC_H

#include <linux/config.h>
#include <linux/irq.h>

#define OPENPIC_SIZE	0x40000

/*
 *  Non-offset'ed vector numbers
 */

#define OPENPIC_VEC_TIMER	110	/* and up */
#define OPENPIC_VEC_IPI		118	/* and up */
#define OPENPIC_VEC_SPURIOUS	255

/* Priorities */
#define OPENPIC_PRIORITY_IPI_BASE	10
#define OPENPIC_PRIORITY_DEFAULT	4
#define OPENPIC_PRIORITY_NMI		9

/* OpenPIC IRQ controller structure */
extern struct hw_interrupt_type open_pic;

/* OpenPIC IPI controller structure */
#ifdef CONFIG_SMP
extern struct hw_interrupt_type open_pic_ipi;
#endif /* CONFIG_SMP */

extern u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;
extern void __iomem * OpenPIC_Addr;
extern int epic_serial_mode;

/* Exported functions */
extern void openpic_set_sources(int first_irq, int num_irqs, void __iomem *isr);
extern void openpic_init(int linux_irq_offset);
extern void openpic_init_nmi_irq(u_int irq);
extern void openpic_set_irq_priority(u_int irq, u_int pri);
extern void openpic_hookup_cascade(u_int irq, char *name,
				   int (*cascade_fn)(struct pt_regs *));
extern u_int openpic_irq(void);
extern void openpic_eoi(void);
extern void openpic_request_IPIs(void);
extern void do_openpic_setup_cpu(void);
extern int openpic_get_irq(struct pt_regs *regs);
extern void openpic_reset_processor_phys(u_int cpumask);
extern void openpic_setup_ISU(int isu_num, unsigned long addr);
extern void openpic_cause_IPI(u_int ipi, cpumask_t cpumask);
extern void smp_openpic_message_pass(int target, int msg);
extern void openpic_set_k2_cascade(int irq);
extern void openpic_set_priority(u_int pri);
extern u_int openpic_get_priority(void);

extern inline int openpic_to_irq(int irq)
{
	/* IRQ 0 usually means 'disabled'.. don't mess with it
	 * exceptions to this (sandpoint maybe?)
	 * shouldn't use openpic_to_irq
	 */
	if (irq != 0){
		return irq += NUM_8259_INTERRUPTS;
	} else {
		return 0;
	}
}
/* Support for second openpic on G5 macs */

// FIXME: To be replaced by sane cascaded controller management */

#define PMAC_OPENPIC2_OFFSET	128

#define OPENPIC2_VEC_TIMER	110	/* and up */
#define OPENPIC2_VEC_IPI	118	/* and up */
#define OPENPIC2_VEC_SPURIOUS	127


extern void* OpenPIC2_Addr;

/* Exported functions */
extern void openpic2_set_sources(int first_irq, int num_irqs, void *isr);
extern void openpic2_init(int linux_irq_offset);
extern void openpic2_init_nmi_irq(u_int irq);
extern u_int openpic2_irq(void);
extern void openpic2_eoi(void);
extern int openpic2_get_irq(struct pt_regs *regs);
extern void openpic2_setup_ISU(int isu_num, unsigned long addr);
#endif /* _PPC_KERNEL_OPEN_PIC_H */
