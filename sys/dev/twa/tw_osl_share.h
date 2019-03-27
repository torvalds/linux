/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_OSL_SHARE_H

#define TW_OSL_SHARE_H


/*
 * Macros, structures and functions shared between OSL and CL,
 * and defined by OSL.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clock.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/stdarg.h>

#include <dev/pci/pcivar.h>

#include <dev/twa/tw_osl_types.h>
#include "opt_twa.h"


#ifdef TWA_DEBUG
#define TW_OSL_DEBUG	TWA_DEBUG
#endif

#ifdef TWA_ENCLOSURE_SUPPORT
#define TW_OSL_ENCLOSURE_SUPPORT
#endif

#define TW_OSL_DRIVER_VERSION_STRING	"3.80.06.003"

#define	TW_OSL_CAN_SLEEP

#ifdef TW_OSL_CAN_SLEEP
typedef TW_VOID			*TW_SLEEP_HANDLE;
#endif /* TW_OSL_CAN_SLEEP */

#define TW_OSL_PCI_CONFIG_ACCESSIBLE

#if _BYTE_ORDER == _BIG_ENDIAN
#define TW_OSL_BIG_ENDIAN
#else
#define TW_OSL_LITTLE_ENDIAN
#endif

#ifdef TW_OSL_DEBUG
extern TW_INT32		TW_OSL_DEBUG_LEVEL_FOR_CL;
#endif /* TW_OSL_DEBUG */


/* Possible return codes from/to Common Layer functions. */
#define TW_OSL_ESUCCESS		0		/* success */
#define TW_OSL_EGENFAILURE	1		/* general failure */
#define TW_OSL_ENOMEM		ENOMEM		/* insufficient memory */
#define TW_OSL_EIO		EIO		/* I/O error */
#define TW_OSL_ETIMEDOUT	ETIMEDOUT	/* time out */
#define TW_OSL_ENOTTY		ENOTTY		/* invalid command */
#define TW_OSL_EBUSY		EBUSY		/* busy -- try later */
#define TW_OSL_EBIG		EFBIG		/* request too big */
#define TW_OSL_EWOULDBLOCK	EWOULDBLOCK	/* sleep timed out */
#define TW_OSL_ERESTART		ERESTART /* sleep terminated by a signal */



#endif /* TW_OSL_SHARE_H */
