/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef ENVIRONMENT_H_
#define ENVIRONMENT_H_

/**
 * @file
 *
 * @brief Types and macros specific to the FreeBSD environment.
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/libkern.h>
#include <machine/bus.h>
#include "opt_isci.h"

typedef int8_t 		S8;
typedef uint8_t		U8;

typedef int16_t		S16;
typedef uint16_t	U16;

typedef int32_t		S32;
typedef uint32_t	U32;

typedef int64_t		S64;
typedef uint64_t	U64;

/* Technically, this should be defined as bus_addr_t, but SCIL makes some
 *  incorrect assumptions in some of its physical address calculations which
 *  necessitate using uint64_t here to avoid compiler warnings.  This is
 *  easier for now than modifying SCIL, and works just as well.
 */
typedef uint64_t	SCI_PHYSICAL_ADDRESS;

typedef U64		SATI_LBA;
typedef void *		FUNCPTR;

#define sci_cb_physical_address_upper(address) ((uint32_t)((address)>>32))
#define sci_cb_physical_address_lower(address) ((uint32_t)((address)&0xFFFFFFFF))
#define sci_cb_make_physical_address(physical_address, address_upper, address_lower) \
	((physical_address) = ((U64)(address_upper))<<32 | (address_lower))

#define INLINE __inline

#define PLACEMENT_HINTS(...)

#define SCIC_SDS_4_ENABLED 1
#define PBG_BUILD 1
#define PHY_MAX_LINK_SPEED_GENERATION 3

/* SCIL defines logging as SCI_LOGGING, but the FreeBSD driver name is ISCI.
	So we define ISCI_LOGGING as the option exported to the kernel, and
	translate it here. */
#ifdef ISCI_LOGGING
#define SCI_LOGGING
#endif

#define __SCI_LIBRARY_MAJOR_VERSION__ 3
#define __SCI_LIBRARY_MINOR_VERSION__ 1
#define __SCI_LIBRARY_BUILD_VERSION__ 7142

#define SATI_TRANSPORT_SUPPORTS_SATA
#define SATI_TRANSPORT_SUPPORTS_SAS
#define USE_ABSTRACT_LIST_FUNCTIONS

#define ASSERT(cond)
#define assert(cond)

#endif /* ENVIRONMENT_H_ */
