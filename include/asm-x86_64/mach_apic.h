#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch defines.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */

#include <asm/genapic.h>

#define INT_DELIVERY_MODE (genapic->int_delivery_mode)
#define INT_DEST_MODE (genapic->int_dest_mode)
#define TARGET_CPUS	  (genapic->target_cpus())
#define vector_allocation_domain	(genapic->vector_allocation_domain)
#define apic_id_registered (genapic->apic_id_registered)
#define init_apic_ldr (genapic->init_apic_ldr)
#define send_IPI_mask (genapic->send_IPI_mask)
#define send_IPI_allbutself (genapic->send_IPI_allbutself)
#define send_IPI_all (genapic->send_IPI_all)
#define cpu_mask_to_apicid (genapic->cpu_mask_to_apicid)
#define phys_pkg_id	(genapic->phys_pkg_id)

#endif /* __ASM_MACH_APIC_H */
