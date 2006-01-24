#ifndef __ASM_IPI_H
#define __ASM_IPI_H

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC InterProcessor Interrupt code.
 *
 * Moved to include file by James Cleverdon from
 * arch/x86-64/kernel/smp.c
 *
 * Copyrights from kernel/smp.c:
 *
 * (c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 * (c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 * (c) 2002,2003 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License, v.2
 */

#include <asm/fixmap.h>
#include <asm/hw_irq.h>
#include <asm/apicdef.h>
#include <asm/genapic.h>

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static inline unsigned int __prepare_ICR (unsigned int shortcut, int vector, unsigned int dest)
{
	unsigned int icr = shortcut | dest;

	switch (vector) {
	default:
		icr |= APIC_DM_FIXED | vector;
		break;
	case NMI_VECTOR:
		icr |= APIC_DM_NMI;
		break;
	}
	return icr;
}

static inline int __prepare_ICR2 (unsigned int mask)
{
	return SET_APIC_DEST_FIELD(mask);
}

static inline void __send_IPI_shortcut(unsigned int shortcut, int vector, unsigned int dest)
{
	/*
	 * Subtle. In the case of the 'never do double writes' workaround
	 * we have to lock out interrupts to be safe.  As we don't care
	 * of the value read we use an atomic rmw access to avoid costly
	 * cli/sti.  Otherwise we use an even cheaper single atomic write
	 * to the APIC.
	 */
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector, dest);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
}


static inline void send_IPI_mask_sequence(cpumask_t mask, int vector)
{
	unsigned long cfg, flags;
	unsigned long query_cpu;

	/*
	 * Hack. The clustered APIC addressing mode doesn't allow us to send
	 * to an arbitrary mask, so I do a unicast to each CPU instead.
	 * - mbligh
	 */
	local_irq_save(flags);

	for_each_cpu_mask(query_cpu, mask) {
		/*
		 * Wait for idle.
		 */
		apic_wait_icr_idle();

		/*
		 * prepare target chip field
		 */
		cfg = __prepare_ICR2(x86_cpu_to_apicid[query_cpu]);
		apic_write(APIC_ICR2, cfg);

		/*
		 * program the ICR
		 */
		cfg = __prepare_ICR(0, vector, APIC_DEST_PHYSICAL);

		/*
		 * Send the IPI. The write to APIC_ICR fires this off.
		 */
		apic_write(APIC_ICR, cfg);
	}
	local_irq_restore(flags);
}

#endif /* __ASM_IPI_H */
