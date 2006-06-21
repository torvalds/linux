/*
 *  include/asm-s390/hardirq.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "include/asm-i386/hardirq.h"
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <asm/lowcore.h>

/* irq_cpustat_t is unused currently, but could be converted
 * into a percpu variable instead of storing softirq_pending
 * on the lowcore */
typedef struct {
	unsigned int __softirq_pending;
} irq_cpustat_t;

#define local_softirq_pending() (S390_lowcore.softirq_pending)

#define __ARCH_IRQ_STAT
#define __ARCH_HAS_DO_SOFTIRQ

#define HARDIRQ_BITS	8

extern void account_ticks(struct pt_regs *);

#endif /* __ASM_HARDIRQ_H */
