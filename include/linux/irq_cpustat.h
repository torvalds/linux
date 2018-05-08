/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __irq_cpustat_h
#define __irq_cpustat_h

/*
 * Contains default mappings for irq_cpustat_t, used by almost every
 * architecture.  Some arch (like s390) have per cpu hardware pages and
 * they define their own mappings for irq_stat.
 *
 * Keith Owens <kaos@ocs.com.au> July 2000.
 */


/*
 * Simple wrappers reducing source bloat.  Define all irq_stat fields
 * here, even ones that are arch dependent.  That way we get common
 * definitions instead of differing sets for each arch.
 */

#ifndef __ARCH_IRQ_STAT
DECLARE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);	/* defined in asm/hardirq.h */
#define __IRQ_STAT(cpu, member)	(per_cpu(irq_stat.member, cpu))
#endif

  /* arch independent irq_stat fields */
#define local_softirq_pending() \
	__IRQ_STAT(smp_processor_id(), __softirq_pending)

  /* arch dependent irq_stat fields */
#define nmi_count(cpu)		__IRQ_STAT((cpu), __nmi_count)	/* i386 */

#endif	/* __irq_cpustat_h */
