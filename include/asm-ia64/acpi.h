/*
 *  asm-ia64/acpi.h
 *
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001,2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#ifdef __KERNEL__

#include <acpi/pdc_intel.h>

#include <linux/init.h>
#include <linux/numa.h>
#include <asm/system.h>

#define COMPILER_DEPENDENT_INT64	long
#define COMPILER_DEPENDENT_UINT64	unsigned long

/*
 * Calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#define ACPI_SYSTEM_XFACE
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE

/* Asm macros */

#define ACPI_ASM_MACROS
#define BREAKPOINT3
#define ACPI_DISABLE_IRQS() local_irq_disable()
#define ACPI_ENABLE_IRQS()  local_irq_enable()
#define ACPI_FLUSH_CPU_CACHE()

static inline int
ia64_acpi_acquire_global_lock (unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = (((old & ~0x3) + 2) + ((old >> 1) & 0x1));
		val = ia64_cmpxchg4_acq(lock, new, old);
	} while (unlikely (val != old));
	return (new < 3) ? -1 : 0;
}

static inline int
ia64_acpi_release_global_lock (unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = old & ~0x3;
		val = ia64_cmpxchg4_acq(lock, new, old);
	} while (unlikely (val != old));
	return old & 0x1;
}

#define ACPI_ACQUIRE_GLOBAL_LOCK(facs, Acq)				\
	((Acq) = ia64_acpi_acquire_global_lock(&facs->global_lock))

#define ACPI_RELEASE_GLOBAL_LOCK(facs, Acq)				\
	((Acq) = ia64_acpi_release_global_lock(&facs->global_lock))

#define acpi_disabled 0	/* ACPI always enabled on IA64 */
#define acpi_noirq 0	/* ACPI always enabled on IA64 */
#define acpi_pci_disabled 0 /* ACPI PCI always enabled on IA64 */
#define acpi_strict 1	/* no ACPI spec workarounds on IA64 */
#define acpi_processor_cstate_check(x) (x) /* no idle limits on IA64 :) */
static inline void disable_acpi(void) { }

const char *acpi_get_sysname (void);
int acpi_request_vector (u32 int_type);
int acpi_gsi_to_irq (u32 gsi, unsigned int *irq);

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern void acpi_restore_state_mem(void);
extern unsigned long acpi_wakeup_address;

/*
 * Record the cpei override flag and current logical cpu. This is
 * useful for CPU removal.
 */
extern unsigned int can_cpei_retarget(void);
extern unsigned int is_cpu_cpei_target(unsigned int cpu);
extern void set_cpei_target_cpu(unsigned int cpu);
extern unsigned int get_cpei_target_cpu(void);
extern void prefill_possible_map(void);
extern int additional_cpus;

#ifdef CONFIG_ACPI_NUMA
#if MAX_NUMNODES > 256
#define MAX_PXM_DOMAINS MAX_NUMNODES
#else
#define MAX_PXM_DOMAINS (256)
#endif
extern int __devinitdata pxm_to_nid_map[MAX_PXM_DOMAINS];
extern int __initdata nid_to_pxm_map[MAX_NUMNODES];
#endif

#define acpi_unlazy_tlb(x)

#endif /*__KERNEL__*/

#endif /*_ASM_ACPI_H*/
