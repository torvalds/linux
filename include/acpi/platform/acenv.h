/******************************************************************************
 *
 * Name: acenv.h - Generation environment specific items
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
 * Configuration for ACPI tools and utilities
 */

#ifdef ACPI_LIBRARY
#define ACPI_USE_LOCAL_CACHE
#endif

#ifdef ACPI_DUMP_APP
#ifndef MSDOS
#define ACPI_DEBUG_OUTPUT
#endif
#define ACPI_APPLICATION
#define ACPI_DISASSEMBLER
#define ACPI_NO_METHOD_EXECUTION
#endif

#ifdef ACPI_EXEC_APP
#undef DEBUGGER_THREADING
#define DEBUGGER_THREADING      DEBUGGER_SINGLE_THREADED
#define ACPI_DEBUG_OUTPUT
#define ACPI_APPLICATION
#define ACPI_DEBUGGER
#define ACPI_DISASSEMBLER
#define ACPI_MUTEX_DEBUG
#endif

#ifdef ACPI_ASL_COMPILER
#define ACPI_DEBUG_OUTPUT
#define ACPI_APPLICATION
#define ACPI_DISASSEMBLER
#define ACPI_CONSTANT_EVAL_ONLY
#endif

#ifdef ACPI_APPLICATION
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_LOCAL_CACHE
#endif

/*
 * Environment configuration.  The purpose of this file is to interface to the
 * local generation environment.
 *
 * 1) ACPI_USE_SYSTEM_CLIBRARY - Define this if linking to an actual C library.
 *      Otherwise, local versions of string/memory functions will be used.
 * 2) ACPI_USE_STANDARD_HEADERS - Define this if linking to a C library and
 *      the standard header files may be used.
 *
 * The ACPI subsystem only uses low level C library functions that do not call
 * operating system services and may therefore be inlined in the code.
 *
 * It may be necessary to tailor these include files to the target
 * generation environment.
 *
 *
 * Functions and constants used from each header:
 *
 * string.h:    memcpy
 *              memset
 *              strcat
 *              strcmp
 *              strcpy
 *              strlen
 *              strncmp
 *              strncat
 *              strncpy
 *
 * stdlib.h:    strtoul
 *
 * stdarg.h:    va_list
 *              va_arg
 *              va_start
 *              va_end
 *
 */

/*! [Begin] no source code translation */

#if defined(__linux__)
#include "aclinux.h"

#elif defined(_AED_EFI)
#include "acefi.h"

#elif defined(WIN32)
#include "acwin.h"

#elif defined(WIN64)
#include "acwin64.h"

#elif defined(MSDOS)		/* Must appear after WIN32 and WIN64 check */
#include "acdos16.h"

#elif defined(__FreeBSD__)
#include "acfreebsd.h"

#elif defined(__NetBSD__)
#include "acnetbsd.h"

#elif defined(MODESTO)
#include "acmodesto.h"

#elif defined(NETWARE)
#include "acnetware.h"

#elif defined(__sun)
#include "acsolaris.h"

#else

/* All other environments */

#define ACPI_USE_STANDARD_HEADERS

#define COMPILER_DEPENDENT_INT64   long long
#define COMPILER_DEPENDENT_UINT64  unsigned long long

#endif

/*
 * Memory allocation tracking.  Used only if
 * 1) This is the debug version
 * 2) This is NOT a 16-bit version of the code (not enough real-mode memory)
 */
#ifdef ACPI_DEBUG_OUTPUT
#if ACPI_MACHINE_WIDTH != 16
#define ACPI_DBG_TRACK_ALLOCATIONS
#endif
#endif

/*! [End] no source code translation !*/

/*
 * Debugger threading model
 * Use single threaded if the entire subsystem is contained in an application
 * Use multiple threaded when the subsystem is running in the kernel.
 *
 * By default the model is single threaded if ACPI_APPLICATION is set,
 * multi-threaded if ACPI_APPLICATION is not set.
 */
#define DEBUGGER_SINGLE_THREADED    0
#define DEBUGGER_MULTI_THREADED     1

#ifndef DEBUGGER_THREADING
#ifdef ACPI_APPLICATION
#define DEBUGGER_THREADING          DEBUGGER_SINGLE_THREADED

#else
#define DEBUGGER_THREADING          DEBUGGER_MULTI_THREADED
#endif
#endif				/* !DEBUGGER_THREADING */

/******************************************************************************
 *
 * C library configuration
 *
 *****************************************************************************/

#define ACPI_IS_ASCII(c)  ((c) < 0x80)

#ifdef ACPI_USE_SYSTEM_CLIBRARY
/*
 * Use the standard C library headers.
 * We want to keep these to a minimum.
 */
#ifdef ACPI_USE_STANDARD_HEADERS
/*
 * Use the standard headers from the standard locations
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#endif				/* ACPI_USE_STANDARD_HEADERS */

/*
 * We will be linking to the standard Clib functions
 */
#define ACPI_STRSTR(s1,s2)      strstr((s1), (s2))
#define ACPI_STRCHR(s1,c)       strchr((s1), (c))
#define ACPI_STRLEN(s)          (acpi_size) strlen((s))
#define ACPI_STRCPY(d,s)        (void) strcpy((d), (s))
#define ACPI_STRNCPY(d,s,n)     (void) strncpy((d), (s), (acpi_size)(n))
#define ACPI_STRNCMP(d,s,n)     strncmp((d), (s), (acpi_size)(n))
#define ACPI_STRCMP(d,s)        strcmp((d), (s))
#define ACPI_STRCAT(d,s)        (void) strcat((d), (s))
#define ACPI_STRNCAT(d,s,n)     strncat((d), (s), (acpi_size)(n))
#define ACPI_STRTOUL(d,s,n)     strtoul((d), (s), (acpi_size)(n))
#define ACPI_MEMCMP(s1,s2,n)    memcmp((const char *)(s1), (const char *)(s2), (acpi_size)(n))
#define ACPI_MEMCPY(d,s,n)      (void) memcpy((d), (s), (acpi_size)(n))
#define ACPI_MEMSET(d,s,n)      (void) memset((d), (s), (acpi_size)(n))

#define ACPI_TOUPPER(i)         toupper((int) (i))
#define ACPI_TOLOWER(i)         tolower((int) (i))
#define ACPI_IS_XDIGIT(i)       isxdigit((int) (i))
#define ACPI_IS_DIGIT(i)        isdigit((int) (i))
#define ACPI_IS_SPACE(i)        isspace((int) (i))
#define ACPI_IS_UPPER(i)        isupper((int) (i))
#define ACPI_IS_PRINT(i)        isprint((int) (i))
#define ACPI_IS_ALPHA(i)        isalpha((int) (i))

#else

/******************************************************************************
 *
 * Not using native C library, use local implementations
 *
 *****************************************************************************/

 /*
  * Use local definitions of C library macros and functions
  * NOTE: The function implementations may not be as efficient
  * as an inline or assembly code implementation provided by a
  * native C library.
  */

#ifndef va_arg

#ifndef _VALIST
#define _VALIST
typedef char *va_list;
#endif				/* _VALIST */

/*
 * Storage alignment properties
 */
#define  _AUPBND                (sizeof (acpi_native_uint) - 1)
#define  _ADNBND                (sizeof (acpi_native_uint) - 1)

/*
 * Variable argument list macro definitions
 */
#define _bnd(X, bnd)            (((sizeof (X)) + (bnd)) & (~(bnd)))
#define va_arg(ap, T)           (*(T *)(((ap) += (_bnd (T, _AUPBND))) - (_bnd (T,_ADNBND))))
#define va_end(ap)              (void) 0
#define va_start(ap, A)         (void) ((ap) = (((char *) &(A)) + (_bnd (A,_AUPBND))))

#endif				/* va_arg */

#define ACPI_STRSTR(s1,s2)      acpi_ut_strstr ((s1), (s2))
#define ACPI_STRCHR(s1,c)       acpi_ut_strchr ((s1), (c))
#define ACPI_STRLEN(s)          (acpi_size) acpi_ut_strlen ((s))
#define ACPI_STRCPY(d,s)        (void) acpi_ut_strcpy ((d), (s))
#define ACPI_STRNCPY(d,s,n)     (void) acpi_ut_strncpy ((d), (s), (acpi_size)(n))
#define ACPI_STRNCMP(d,s,n)     acpi_ut_strncmp ((d), (s), (acpi_size)(n))
#define ACPI_STRCMP(d,s)        acpi_ut_strcmp ((d), (s))
#define ACPI_STRCAT(d,s)        (void) acpi_ut_strcat ((d), (s))
#define ACPI_STRNCAT(d,s,n)     acpi_ut_strncat ((d), (s), (acpi_size)(n))
#define ACPI_STRTOUL(d,s,n)     acpi_ut_strtoul ((d), (s), (acpi_size)(n))
#define ACPI_MEMCMP(s1,s2,n)    acpi_ut_memcmp((const char *)(s1), (const char *)(s2), (acpi_size)(n))
#define ACPI_MEMCPY(d,s,n)      (void) acpi_ut_memcpy ((d), (s), (acpi_size)(n))
#define ACPI_MEMSET(d,v,n)      (void) acpi_ut_memset ((d), (v), (acpi_size)(n))
#define ACPI_TOUPPER            acpi_ut_to_upper
#define ACPI_TOLOWER            acpi_ut_to_lower

#endif				/* ACPI_USE_SYSTEM_CLIBRARY */

/******************************************************************************
 *
 * Assembly code macros
 *
 *****************************************************************************/

/*
 * Handle platform- and compiler-specific assembly language differences.
 * These should already have been defined by the platform includes above.
 *
 * Notes:
 * 1) Interrupt 3 is used to break into a debugger
 * 2) Interrupts are turned off during ACPI register setup
 */

/* Unrecognized compiler, use defaults */

#ifndef ACPI_ASM_MACROS

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

#define ACPI_ASM_MACROS
#define BREAKPOINT3
#define ACPI_DISABLE_IRQS()
#define ACPI_ENABLE_IRQS()
#define ACPI_ACQUIRE_GLOBAL_LOCK(Glptr, acq)
#define ACPI_RELEASE_GLOBAL_LOCK(Glptr, acq)

#endif				/* ACPI_ASM_MACROS */

#ifdef ACPI_APPLICATION

/* Don't want software interrupts within a ring3 application */

#undef BREAKPOINT3
#define BREAKPOINT3
#endif

/******************************************************************************
 *
 * Compiler-specific information is contained in the compiler-specific
 * headers.
 *
 *****************************************************************************/
#endif				/* __ACENV_H__ */
