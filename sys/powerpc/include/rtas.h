/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
 * $FreeBSD$
 */

#ifndef _MACHINE_RTAS_H_
#define _MACHINE_RTAS_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <dev/ofw/openfirm.h>

/*
 * RTAS functions are defined by 32-bit integer tokens. These vary from
 * system to system, and can be looked up from their standardized names
 * using rtas_token_lookup(). If RTAS is not available, rtas_token_lookup()
 * and rtas_call_method() return -1; this can be checked in advance using
 * rtas_exists(). Otherwise, rtas_call_method() returns one of the RTAS
 * status codes from the bottom of this file.
 */

int rtas_exists(void);
int rtas_call_method(cell_t token, int nargs, int nreturns, ...);
cell_t rtas_token_lookup(const char *method);

/* RTAS Status Codes: see CHRP or PAPR specification */
#define	RTAS_OK				0
#define	RTAS_HW_ERROR			-1
#define	RTAS_BUSY			-2
#define	RTAS_PARAM_ERROR		-3
#define	RTAS_STATE_CHANGE		-7
#define	RTAS_VENDOR_BEGIN		9000
#define	RTAS_EXTENDED_DELAY		9900
#define	RTAS_ISOLATION_ERROR		-9000
#define	RTAS_VENDOR_ERROR_BEGIN		-9004

#endif /* _MACHINE_RTAS_H_ */

