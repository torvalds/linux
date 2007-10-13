/* -*- mode: c; c-basic-offset: 8 -*- */

/* Copyright (C) 2002
 *
 * Author: James.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/voyager/entry_arch.h
 *
 * This file builds the VIC and QIC CPI gates
 */

/* initialise the voyager interrupt gates 
 *
 * This uses the macros in irq.h to set up assembly jump gates.  The
 * calls are then redirected to the same routine with smp_ prefixed */
BUILD_INTERRUPT(vic_sys_interrupt, VIC_SYS_INT)
BUILD_INTERRUPT(vic_cmn_interrupt, VIC_CMN_INT)
BUILD_INTERRUPT(vic_cpi_interrupt, VIC_CPI_LEVEL0);

/* do all the QIC interrupts */
BUILD_INTERRUPT(qic_timer_interrupt, QIC_TIMER_CPI);
BUILD_INTERRUPT(qic_invalidate_interrupt, QIC_INVALIDATE_CPI);
BUILD_INTERRUPT(qic_reschedule_interrupt, QIC_RESCHEDULE_CPI);
BUILD_INTERRUPT(qic_enable_irq_interrupt, QIC_ENABLE_IRQ_CPI);
BUILD_INTERRUPT(qic_call_function_interrupt, QIC_CALL_FUNCTION_CPI);

