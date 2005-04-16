/* -*- mode: c; c-basic-offset: 8 -*- */

/* Copyright (C) 2002
 *
 * Author: James.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/voyager/irq_vectors.h
 *
 * This file provides definitions for the VIC and QIC CPIs
 */

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

/* These define the CPIs we use in linux */
#define VIC_CPI_LEVEL0			0
#define VIC_CPI_LEVEL1			1
/* now the fake CPIs */
#define VIC_TIMER_CPI			2
#define VIC_INVALIDATE_CPI		3
#define VIC_RESCHEDULE_CPI		4
#define VIC_ENABLE_IRQ_CPI		5
#define VIC_CALL_FUNCTION_CPI		6

/* Now the QIC CPIs:  Since we don't need the two initial levels,
 * these are 2 less than the VIC CPIs */
#define QIC_CPI_OFFSET			1
#define QIC_TIMER_CPI			(VIC_TIMER_CPI - QIC_CPI_OFFSET)
#define QIC_INVALIDATE_CPI		(VIC_INVALIDATE_CPI - QIC_CPI_OFFSET)
#define QIC_RESCHEDULE_CPI		(VIC_RESCHEDULE_CPI - QIC_CPI_OFFSET)
#define QIC_ENABLE_IRQ_CPI		(VIC_ENABLE_IRQ_CPI - QIC_CPI_OFFSET)
#define QIC_CALL_FUNCTION_CPI		(VIC_CALL_FUNCTION_CPI - QIC_CPI_OFFSET)

#define VIC_START_FAKE_CPI		VIC_TIMER_CPI
#define VIC_END_FAKE_CPI		VIC_CALL_FUNCTION_CPI

/* this is the SYS_INT CPI. */
#define VIC_SYS_INT			8
#define VIC_CMN_INT			15

/* This is the boot CPI for alternate processors.  It gets overwritten
 * by the above once the system has activated all available processors */
#define VIC_CPU_BOOT_CPI		VIC_CPI_LEVEL0
#define VIC_CPU_BOOT_ERRATA_CPI		(VIC_CPI_LEVEL0 + 8)

#define NR_VECTORS 256
#define NR_IRQS 224
#define NR_IRQ_VECTORS NR_IRQS

#define FPU_IRQ				13

#define	FIRST_VM86_IRQ		3
#define LAST_VM86_IRQ		15
#define invalid_vm86_irq(irq)	((irq) < 3 || (irq) > 15)

#ifndef __ASSEMBLY__
extern asmlinkage void vic_cpi_interrupt(void);
extern asmlinkage void vic_sys_interrupt(void);
extern asmlinkage void vic_cmn_interrupt(void);
extern asmlinkage void qic_timer_interrupt(void);
extern asmlinkage void qic_invalidate_interrupt(void);
extern asmlinkage void qic_reschedule_interrupt(void);
extern asmlinkage void qic_enable_irq_interrupt(void);
extern asmlinkage void qic_call_function_interrupt(void);
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IRQ_VECTORS_H */
