/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acintel.h - VC specific defines, etc.
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACINTEL_H__
#define __ACINTEL_H__

/*
 * Use compiler specific <stdarg.h> is a good practice for even when
 * -nostdinc is specified (i.e., ACPI_USE_STANDARD_HEADERS undefined.
 */
#ifndef va_arg
#include <stdarg.h>
#endif

/* Configuration specific to Intel 64-bit C compiler */

#define COMPILER_DEPENDENT_INT64    __int64
#define COMPILER_DEPENDENT_UINT64   unsigned __int64
#define ACPI_INLINE                 __inline

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

/* remark 981 - operands evaluated in no particular order */
#pragma warning(disable:981)

/* warn C4100: unreferenced formal parameter */
#pragma warning(disable:4100)

/* warn C4127: conditional expression is constant */
#pragma warning(disable:4127)

/* warn C4706: assignment within conditional expression */
#pragma warning(disable:4706)

/* warn C4214: bit field types other than int */
#pragma warning(disable:4214)

#endif				/* __ACINTEL_H__ */
