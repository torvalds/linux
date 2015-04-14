/******************************************************************************
 *
 * Name: aclinuxex.h - Extra OS specific defines, etc. for Linux
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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

#ifndef __ACLINUXEX_H__
#define __ACLINUXEX_H__

#ifdef __KERNEL__

#ifndef ACPI_USE_NATIVE_DIVIDE

#ifndef ACPI_DIV_64_BY_32
#define ACPI_DIV_64_BY_32(n_hi, n_lo, d32, q32, r32) \
	do { \
		u64 (__n) = ((u64) n_hi) << 32 | (n_lo); \
		(r32) = do_div ((__n), (d32)); \
		(q32) = (u32) (__n); \
	} while (0)
#endif

#ifndef ACPI_SHIFT_RIGHT_64
#define ACPI_SHIFT_RIGHT_64(n_hi, n_lo) \
	do { \
		(n_lo) >>= 1; \
		(n_lo) |= (((n_hi) & 1) << 31); \
		(n_hi) >>= 1; \
	} while (0)
#endif

#endif

/*
 * Overrides for in-kernel ACPICA
 */
acpi_status __init acpi_os_initialize(void);

acpi_status acpi_os_terminate(void);

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

static inline void *acpi_os_allocate_zeroed(acpi_size size)
{
	return kzalloc(size, irqs_disabled()? GFP_ATOMIC : GFP_KERNEL);
}

static inline void acpi_os_free(void *memory)
{
	kfree(memory);
}

static inline void *acpi_os_acquire_object(acpi_cache_t * cache)
{
	return kmem_cache_zalloc(cache,
				 irqs_disabled()? GFP_ATOMIC : GFP_KERNEL);
}

static inline acpi_thread_id acpi_os_get_thread_id(void)
{
	return (acpi_thread_id) (unsigned long)current;
}

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

/*
 * OSL interfaces added by Linux
 */
void early_acpi_os_unmap_memory(void __iomem * virt, acpi_size size);

#endif				/* __KERNEL__ */

#endif				/* __ACLINUXEX_H__ */
