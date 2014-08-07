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

#ifdef __KERNEL__

/* ACPICA external files should not include ACPICA headers directly. */

#if !defined(BUILDING_ACPICA) && !defined(_LINUX_ACPI_H)
#error "Please don't include <acpi/acpi.h> directly, include <linux/acpi.h> instead."
#endif

#endif

/* Common (in-kernel/user-space) ACPICA configuration */

#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_DO_WHILE_0

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
#ifdef CONFIG_ACPI
#include <asm/acenv.h>
#endif

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
#define ACPI_EXPORT_SYMBOL(symbol)  EXPORT_SYMBOL(symbol);
#define strtoul                     simple_strtoul

#define acpi_cache_t                        struct kmem_cache
#define acpi_spinlock                       spinlock_t *
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

#else				/* !__KERNEL__ */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

/* Define/disable kernel-specific declarators */

#ifndef __init
#define __init
#endif

/* Host-dependent types and defines for user-space ACPICA */

#define ACPI_FLUSH_CPU_CACHE()
#define ACPI_CAST_PTHREAD_T(pthread) ((acpi_thread_id) (pthread))

#if defined(__ia64__)    || defined(__x86_64__) ||\
	defined(__aarch64__) || defined(__PPC64__)
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

#endif				/* __ACLINUX_H__ */
