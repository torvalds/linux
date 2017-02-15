/******************************************************************************
 *
 * Name: acenvex.h - Extra host and compiler configuration
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#ifndef __ACENVEX_H__
#define __ACENVEX_H__

/*! [Begin] no source code translation */

/******************************************************************************
 *
 * Extra host configuration files. All ACPICA headers are included before
 * including these files.
 *
 *****************************************************************************/

#if defined(_LINUX) || defined(__linux__)
#include <acpi/platform/aclinuxex.h>

#elif defined(__DragonFly__)
#include "acdragonflyex.h"

/*
 * EFI applications can be built with -nostdlib, in this case, it must be
 * included after including all other host environmental definitions, in
 * order to override the definitions.
 */
#elif defined(_AED_EFI) || defined(_GNU_EFI) || defined(_EDK2_EFI)
#include "acefiex.h"

#endif

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#include "acgccex.h"

#elif defined(_MSC_VER)
#include "acmsvcex.h"

#endif

/*! [End] no source code translation !*/

#endif				/* __ACENVEX_H__ */
