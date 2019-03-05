/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: aclinux.h - OS specific defines, etc. for Linux
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACLINUX_H__
#define __ACLINUX_H__

#ifdef __KERNEL__

/* ACPICA external files should not include ACPICA headers directly. */

#if !defined(BUILDING_ACPICA) && !defined(_LINUX_ACPI_H)
#error "Please don't include <acpi/acpi.h> directly, include <linux/acpi.h> instead."
#endif

#endif

/* Common (in-kernel/user-space) ACPICA configuration */

#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_DO_WHILE_0
#define ACPI_IGNORE_PACKAGE_RESOLUTION_ERRORS

#ifdef __KERNEL__

#define ACPI_USE_SYSTEM_INTTYPES
#define ACPI_USE_GPE_POLLING

/* Kernel specific ACPICA configuration */

#ifdef CONFIG_PCI
#define ACPI_PCI_CONFIGURED
#endif

#ifdef CONFIG_ACPI_REDUCED_HARDWARE_ONLY
#define ACPI_REDUCED_HARDWARE 1
#endif

#ifdef CONFIG_ACPI_DEBUGGER
#define ACPI_DEBUGGER
#endif

#ifdef CONFIG_ACPI_DEBUG
#define ACPI_MUTEX_DEBUG
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
#ifdef CONFIG_ACPI
#include <asm/acenv.h>
#endif

#define ACPI_INIT_FUNCTION __init

#ifndef CONFIG_ACPI

/* External globals for __KERNEL__, stubs is needed */

#define ACPI_GLOBAL(t,a)
#define ACPI_INIT_GLOBAL(t,a,b)

/* Generating stubs for configurable ACPICA macros */

#define ACPI_NO_MEM_ALLOCATIONS

/* Generating stubs for configurable ACPICA functions */

#define ACPI_NO_ERROR_MESSAGES
#undef ACPI_DEBUG_OUTPUT

/* External interface for __KERNEL__, stub is needed */

#define ACPI_EXTERNAL_RETURN_STATUS(prototype) \
	static ACPI_INLINE prototype {return(AE_NOT_CONFIGURED);}
#define ACPI_EXTERNAL_RETURN_OK(prototype) \
	static ACPI_INLINE prototype {return(AE_OK);}
#define ACPI_EXTERNAL_RETURN_VOID(prototype) \
	static ACPI_INLINE prototype {return;}
#define ACPI_EXTERNAL_RETURN_UINT32(prototype) \
	static ACPI_INLINE prototype {return(0);}
#define ACPI_EXTERNAL_RETURN_PTR(prototype) \
	static ACPI_INLINE prototype {return(NULL);}

#endif				/* CONFIG_ACPI */

/* Host-dependent types and defines for in-kernel ACPICA */

#define ACPI_MACHINE_WIDTH          BITS_PER_LONG
#define ACPI_USE_NATIVE_MATH64
#define ACPI_EXPORT_SYMBOL(symbol)  EXPORT_SYMBOL(symbol);
#define strtoul                     simple_strtoul

#define acpi_cache_t                        struct kmem_cache
#define acpi_spinlock                       spinlock_t *
#define acpi_raw_spinlock                   raw_spinlock_t *
#define acpi_cpu_flags                      unsigned long

/* Use native linux version of acpi_os_allocate_zeroed */

#define USE_NATIVE_ALLOCATE_ZEROED

/*
 * Overrides for in-kernel ACPICA
 */
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_initialize
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_terminate
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_allocate
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_allocate_zeroed
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_free
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_acquire_object
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_thread_id
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_create_lock
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_create_raw_lock
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_delete_raw_lock
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_acquire_raw_lock
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_release_raw_lock

/*
 * OSL interfaces used by debugger/disassembler
 */
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_readable
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_writable
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_initialize_debugger
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_terminate_debugger

/*
 * OSL interfaces used by utilities
 */
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_redirect_output
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_name
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_index
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_table_by_address
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_open_directory
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_next_filename
#define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_close_directory

#define ACPI_MSG_ERROR          KERN_ERR "ACPI Error: "
#define ACPI_MSG_EXCEPTION      KERN_ERR "ACPI Exception: "
#define ACPI_MSG_WARNING        KERN_WARNING "ACPI Warning: "
#define ACPI_MSG_INFO           KERN_INFO "ACPI: "

#define ACPI_MSG_BIOS_ERROR     KERN_ERR "ACPI BIOS Error (bug): "
#define ACPI_MSG_BIOS_WARNING   KERN_WARNING "ACPI BIOS Warning (bug): "

/*
 * Linux wants to use designated initializers for function pointer structs.
 */
#define ACPI_STRUCT_INIT(field, value)	.field = value

#else				/* !__KERNEL__ */

#define ACPI_USE_STANDARD_HEADERS

#ifdef ACPI_USE_STANDARD_HEADERS
#include <unistd.h>
#endif

/* Define/disable kernel-specific declarators */

#ifndef __init
#define __init
#endif
#ifndef __iomem
#define __iomem
#endif

/* Host-dependent types and defines for user-space ACPICA */

#define ACPI_FLUSH_CPU_CACHE()
#define ACPI_CAST_PTHREAD_T(pthread) ((acpi_thread_id) (pthread))

#if defined(__ia64__)    || (defined(__x86_64__) && !defined(__ILP32__)) ||\
	defined(__aarch64__) || defined(__PPC64__) ||\
	defined(__s390x__)
#define ACPI_MACHINE_WIDTH          64
#define COMPILER_DEPENDENT_INT64    long
#define COMPILER_DEPENDENT_UINT64   unsigned long
#else
#define ACPI_MACHINE_WIDTH          32
#define COMPILER_DEPENDENT_INT64    long long
#define COMPILER_DEPENDENT_UINT64   unsigned long long
#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_NATIVE_MATH64
#endif

#ifndef __cdecl
#define __cdecl
#endif

#endif				/* __KERNEL__ */

#endif				/* __ACLINUX_H__ */
