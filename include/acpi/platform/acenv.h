/******************************************************************************
 *
 * Name: acenv.h - Host and compiler configuration
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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

#ifndef __ACENV_H__
#define __ACENV_H__

/*
 * Environment configuration. The purpose of this file is to interface ACPICA
 * to the local environment. This includes compiler-specific, OS-specific,
 * and machine-specific configuration.
 */

/* Types for ACPI_MUTEX_TYPE */

#define ACPI_BINARY_SEMAPHORE       0
#define ACPI_OSL_MUTEX              1

/* Types for DEBUGGER_THREADING */

#define DEBUGGER_SINGLE_THREADED    0
#define DEBUGGER_MULTI_THREADED     1

/******************************************************************************
 *
 * Configuration for ACPI tools and utilities
 *
 *****************************************************************************/

/* Common application configuration. All single threaded except for acpi_exec. */

#if (defined ACPI_ASL_COMPILER) || \
	(defined ACPI_BIN_APP)      || \
	(defined ACPI_DUMP_APP)     || \
	(defined ACPI_HELP_APP)     || \
	(defined ACPI_NAMES_APP)    || \
	(defined ACPI_SRC_APP)      || \
	(defined ACPI_XTRACT_APP)   || \
	(defined ACPI_EXAMPLE_APP)
#define ACPI_APPLICATION
#define ACPI_SINGLE_THREADED
#define USE_NATIVE_ALLOCATE_ZEROED
#endif

/* iASL configuration */

#ifdef ACPI_ASL_COMPILER
#define ACPI_DEBUG_OUTPUT
#define ACPI_CONSTANT_EVAL_ONLY
#define ACPI_LARGE_NAMESPACE_NODE
#define ACPI_DATA_TABLE_DISASSEMBLY
#define ACPI_32BIT_PHYSICAL_ADDRESS
#define ACPI_DISASSEMBLER 1
#endif

/* acpi_exec configuration. Multithreaded with full AML debugger */

#ifdef ACPI_EXEC_APP
#define ACPI_APPLICATION
#define ACPI_FULL_DEBUG
#define ACPI_MUTEX_DEBUG
#define ACPI_DBG_TRACK_ALLOCATIONS
#endif

/* acpi_help configuration. Error messages disabled. */

#ifdef ACPI_HELP_APP
#define ACPI_NO_ERROR_MESSAGES
#endif

/* acpi_names configuration. Debug output enabled. */

#ifdef ACPI_NAMES_APP
#define ACPI_DEBUG_OUTPUT
#endif

/* acpi_exec/acpi_names/Example configuration. Native RSDP used. */

#if (defined ACPI_EXEC_APP)     || \
	(defined ACPI_EXAMPLE_APP)  || \
	(defined ACPI_NAMES_APP)
#define ACPI_USE_NATIVE_RSDP_POINTER
#endif

/* acpi_dump configuration. Native mapping used if provided by the host */

#ifdef ACPI_DUMP_APP
#define ACPI_USE_NATIVE_MEMORY_MAPPING
#endif

/* acpi_names/Example configuration. Hardware disabled */

#if (defined ACPI_EXAMPLE_APP)  || \
	(defined ACPI_NAMES_APP)
#define ACPI_REDUCED_HARDWARE 1
#endif

/* Linkable ACPICA library. Two versions, one with full debug. */

#ifdef ACPI_LIBRARY
#define ACPI_USE_LOCAL_CACHE
#define ACPI_DEBUGGER 1
#define ACPI_DISASSEMBLER 1

#ifdef _DEBUG
#define ACPI_DEBUG_OUTPUT
#endif
#endif

/* Common for all ACPICA applications */

#ifdef ACPI_APPLICATION
#define ACPI_USE_LOCAL_CACHE
#endif

/* Common debug/disassembler support */

#ifdef ACPI_FULL_DEBUG
#define ACPI_DEBUG_OUTPUT
#define ACPI_DEBUGGER 1
#define ACPI_DISASSEMBLER 1
#endif


/*! [Begin] no source code translation */

/******************************************************************************
 *
 * Host configuration files. The compiler configuration files are included
 * first.
 *
 *****************************************************************************/

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#include <acpi/platform/acgcc.h>

#elif defined(_MSC_VER)
#include "acmsvc.h"

#elif defined(__INTEL_COMPILER)
#include <acpi/platform/acintel.h>

#endif

#if defined(_LINUX) || defined(__linux__)
#include <acpi/platform/aclinux.h>

#elif defined(_APPLE) || defined(__APPLE__)
#include "acmacosx.h"

#elif defined(__DragonFly__)
#include "acdragonfly.h"

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include "acfreebsd.h"

#elif defined(__NetBSD__)
#include "acnetbsd.h"

#elif defined(__sun)
#include "acsolaris.h"

#elif defined(MODESTO)
#include "acmodesto.h"

#elif defined(NETWARE)
#include "acnetware.h"

#elif defined(_CYGWIN)
#include "accygwin.h"

#elif defined(WIN32)
#include "acwin.h"

#elif defined(WIN64)
#include "acwin64.h"

#elif defined(_WRS_LIB_BUILD)
#include "acvxworks.h"

#elif defined(__OS2__)
#include "acos2.h"

#elif defined(__HAIKU__)
#include "achaiku.h"

#elif defined(__QNX__)
#include "acqnx.h"

/*
 * EFI applications can be built with -nostdlib, in this case, it must be
 * included after including all other host environmental definitions, in
 * order to override the definitions.
 */
#elif defined(_AED_EFI) || defined(_GNU_EFI) || defined(_EDK2_EFI)
#include "acefi.h"

#else

/* Unknown environment */

#error Unknown target environment
#endif

/*! [End] no source code translation !*/

/******************************************************************************
 *
 * Setup defaults for the required symbols that were not defined in one of
 * the host/compiler files above.
 *
 *****************************************************************************/

/* 64-bit data types */

#ifndef COMPILER_DEPENDENT_INT64
#define COMPILER_DEPENDENT_INT64   long long
#endif

#ifndef COMPILER_DEPENDENT_UINT64
#define COMPILER_DEPENDENT_UINT64  unsigned long long
#endif

/* Type of mutex supported by host. Default is binary semaphores. */
#ifndef ACPI_MUTEX_TYPE
#define ACPI_MUTEX_TYPE             ACPI_BINARY_SEMAPHORE
#endif

/* Global Lock acquire/release */

#ifndef ACPI_ACQUIRE_GLOBAL_LOCK
#define ACPI_ACQUIRE_GLOBAL_LOCK(Glptr, acquired) acquired = 1
#endif

#ifndef ACPI_RELEASE_GLOBAL_LOCK
#define ACPI_RELEASE_GLOBAL_LOCK(Glptr, pending) pending = 0
#endif

/* Flush CPU cache - used when going to sleep. Wbinvd or similar. */

#ifndef ACPI_FLUSH_CPU_CACHE
#define ACPI_FLUSH_CPU_CACHE()
#endif

/* "inline" keywords - configurable since inline is not standardized */

#ifndef ACPI_INLINE
#define ACPI_INLINE
#endif

/*
 * Configurable calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#ifndef ACPI_SYSTEM_XFACE
#define ACPI_SYSTEM_XFACE
#endif

#ifndef ACPI_EXTERNAL_XFACE
#define ACPI_EXTERNAL_XFACE
#endif

#ifndef ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#endif

#ifndef ACPI_INTERNAL_VAR_XFACE
#define ACPI_INTERNAL_VAR_XFACE
#endif

/*
 * Debugger threading model
 * Use single threaded if the entire subsystem is contained in an application
 * Use multiple threaded when the subsystem is running in the kernel.
 *
 * By default the model is single threaded if ACPI_APPLICATION is set,
 * multi-threaded if ACPI_APPLICATION is not set.
 */
#ifndef DEBUGGER_THREADING
#if !defined (ACPI_APPLICATION) || defined (ACPI_EXEC_APP)
#define DEBUGGER_THREADING          DEBUGGER_MULTI_THREADED

#else
#define DEBUGGER_THREADING          DEBUGGER_SINGLE_THREADED
#endif
#endif				/* !DEBUGGER_THREADING */

/******************************************************************************
 *
 * C library configuration
 *
 *****************************************************************************/

/*
 * ACPI_USE_SYSTEM_CLIBRARY - Define this if linking to an actual C library.
 *      Otherwise, local versions of string/memory functions will be used.
 * ACPI_USE_STANDARD_HEADERS - Define this if linking to a C library and
 *      the standard header files may be used. Defining this implies that
 *      ACPI_USE_SYSTEM_CLIBRARY has been defined.
 *
 * The ACPICA subsystem only uses low level C library functions that do not
 * call operating system services and may therefore be inlined in the code.
 *
 * It may be necessary to tailor these include files to the target
 * generation environment.
 */

/* Use the standard C library headers. We want to keep these to a minimum. */

#ifdef ACPI_USE_STANDARD_HEADERS

/* Use the standard headers from the standard locations */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef ACPI_APPLICATION
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#endif

#endif				/* ACPI_USE_STANDARD_HEADERS */

#ifdef ACPI_APPLICATION
#define ACPI_FILE              FILE *
#define ACPI_FILE_OUT          stdout
#define ACPI_FILE_ERR          stderr
#else
#define ACPI_FILE              void *
#define ACPI_FILE_OUT          NULL
#define ACPI_FILE_ERR          NULL
#endif				/* ACPI_APPLICATION */

#ifndef ACPI_INIT_FUNCTION
#define ACPI_INIT_FUNCTION
#endif

#endif				/* __ACENV_H__ */
