/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: aclinuxex.h - Extra OS specific defines, etc. for Linux
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

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
acpi_status ACPI_INIT_FUNCTION acpi_os_initialize(void);

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


#define acpi_os_create_raw_lock(__handle) \
	({ \
		raw_spinlock_t *lock = ACPI_ALLOCATE(sizeof(*lock)); \
		if (lock) { \
			*(__handle) = lock; \
			raw_spin_lock_init(*(__handle)); \
		} \
		lock ? AE_OK : AE_NO_MEMORY; \
	})

static inline acpi_cpu_flags acpi_os_acquire_raw_lock(acpi_raw_spinlock lockp)
{
	acpi_cpu_flags flags;

	raw_spin_lock_irqsave(lockp, flags);
	return flags;
}

static inline void acpi_os_release_raw_lock(acpi_raw_spinlock lockp,
					    acpi_cpu_flags flags)
{
	raw_spin_unlock_irqrestore(lockp, flags);
}

static inline void acpi_os_delete_raw_lock(acpi_raw_spinlock handle)
{
	ACPI_FREE(handle);
}

static inline u8 acpi_os_readable(void *pointer, acpi_size length)
{
	return TRUE;
}

static inline acpi_status acpi_os_initialize_debugger(void)
{
	return AE_OK;
}

static inline void acpi_os_terminate_debugger(void)
{
	return;
}

/*
 * OSL interfaces added by Linux
 */

#endif				/* __KERNEL__ */

#endif				/* __ACLINUXEX_H__ */
