/******************************************************************************
 *
 * Name: aclinux.h - OS specific defines, etc. for Linux
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACLINUX_H__
#define __ACLINUX_H__

/* Common (in-kernel/user-space) ACPICA configuration */

#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_DO_WHILE_0
#define ACPI_MUTEX_TYPE             ACPI_BINARY_SEMAPHORE

#ifdef __KERNEL__

#define ACPI_USE_SYSTEM_INTTYPES

/* Compile for reduced hardware mode only with this kernel config */

#ifdef CONFIG_ACPI_REDUCED_HARDWARE_ONLY
#define ACPI_REDUCED_HARDWARE 1
#endif

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#ifdef EXPORT_ACPI_INTERFACES
#include <linux/export.h>
#endif
#include <asm/acpi.h>

/* Host-dependent types and defines for in-kernel ACPICA */

#define ACPI_MACHINE_WIDTH          BITS_PER_LONG
#define ACPI_EXPORT_SYMBOL(symbol)  EXPORT_SYMBOL(symbol);
#define strtoul                     simple_strtoul

#define acpi_cache_t                        struct kmem_cache
#define acpi_spinlock                       spinlock_t *
#define acpi_cpu_flags                      unsigned long

#else				/* !__KERNEL__ */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

/* Disable kernel specific declarators */

#ifndef __init
#define __init
#endif

#ifndef __iomem
#define __iomem
#endif

/* Host-dependent types and defines for user-space ACPICA */

#define ACPI_FLUSH_CPU_CACHE()
#define ACPI_CAST_PTHREAD_T(pthread) ((acpi_thread_id) (pthread))

#if defined(__ia64__) || defined(__x86_64__) || defined(__aarch64__)
#define ACPI_MACHINE_WIDTH          64
#define COMPILER_DEPENDENT_INT64    long
#define COMPILER_DEPENDENT_UINT64   unsigned long
#else
#define ACPI_MACHINE_WIDTH          32
#define COMPILER_DEPENDENT_INT64    long long
#define COMPILER_DEPENDENT_UINT64   unsigned long long
#define ACPI_USE_NATIVE_DIVIDE
#endif

#ifndef __cdecl
#define __cdecl
#endif

#endif				/* __KERNEL__ */

/* Linux uses GCC */

#include <acpi/platform/acgcc.h>

#ifdef __KERNEL__

/*
 * FIXME: Inclusion of actypes.h
 * Linux kernel need this before defining inline OSL interfaces as
 * actypes.h need to be included to find ACPICA type definitions.
 * Since from ACPICA's perspective, the actypes.h should be included after
 * acenv.h (aclinux.h), this leads to a inclusion mis-ordering issue.
 */
#include <acpi/actypes.h>

/*
 * Overrides for in-kernel ACPICA
 */
acpi_status __init acpi_os_initialize(void);
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_initialize

acpi_status acpi_os_terminate(void);
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_terminate

/*
 * Memory allocation/deallocation
 */

/*
 * The irqs_disabled() check is for resume from RAM.
 * Interrupts are off during resume, just like they are for boot.
 * However, boot has  (system_state != SYSTEM_RUNNING)
 * to quiet __might_sleep() in kmalloc() and resume does not.
 */
static inline void *acpi_os_allocate(acpi_size size)
{
	return kmalloc(size, irqs_disabled()? GFP_ATOMIC : GFP_KERNEL);
}

#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_allocate

/* Use native linux version of acpi_os_allocate_zeroed */

static inline void *acpi_os_allocate_zeroed(acpi_size size)
{
	return kzalloc(size, irqs_disabled()? GFP_ATOMIC : GFP_KERNEL);
}

#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_allocate_zeroed
#define USE_NATIVE_ALLOCATE_ZEROED

static inline void acpi_os_free(void *memory)
{
	kfree(memory);
}

#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_free

static inline void *acpi_os_acquire_object(acpi_cache_t * cache)
{
	return kmem_cache_zalloc(cache,
				 irqs_disabled()? GFP_ATOMIC : GFP_KERNEL);
}

#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_acquire_object

static inline acpi_thread_id acpi_os_get_thread_id(void)
{
	return (acpi_thread_id) (unsigned long)current;
}

#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_thread_id

#ifndef CONFIG_PREEMPT

/*
 * Used within ACPICA to show where it is safe to preempt execution
 * when CONFIG_PREEMPT=n
 */
#define ACPI_PREEMPTION_POINT() \
	do { \
		if (!irqs_disabled()) \
			cond_resched(); \
	} while (0)

#endif

/*
 * When lockdep is enabled, the spin_lock_init() macro stringifies it's
 * argument and uses that as a name for the lock in debugging.
 * By executing spin_lock_init() in a macro the key changes from "lock" for
 * all locks to the name of the argument of acpi_os_create_lock(), which
 * prevents lockdep from reporting false positives for ACPICA locks.
 */
#define acpi_os_create_lock(__handle) \
	({ \
		spinlock_t *lock = ACPI_ALLOCATE(sizeof(*lock)); \
		if (lock) { \
			*(__handle) = lock; \
			spin_lock_init(*(__handle)); \
		} \
		lock ? AE_OK : AE_NO_MEMORY; \
	})
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_create_lock

void __iomem *acpi_os_map_memory(acpi_physical_address where, acpi_size length);
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_map_memory

void acpi_os_unmap_memory(void __iomem * logical_address, acpi_size size);
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_unmap_memory

/*
 * OSL interfaces used by debugger/disassembler
 */
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_readable
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_writable

/*
 * OSL interfaces used by utilities
 */
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_redirect_output
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_line
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_name
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_index
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_address
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_open_directory
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_next_filename
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_close_directory

/*
 * OSL interfaces added by Linux
 */
void early_acpi_os_unmap_memory(void __iomem * virt, acpi_size size);

#endif				/* __KERNEL__ */

#endif				/* __ACLINUX_H__ */
