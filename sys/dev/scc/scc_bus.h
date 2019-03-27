/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_SCC_BUS_H_
#define	_DEV_SCC_BUS_H_

#include <sys/serial.h>
#include <serdev_if.h>

#define	SCC_IVAR_CHANNEL	0
#define	SCC_IVAR_CLASS		1
#define	SCC_IVAR_CLOCK		2
#define	SCC_IVAR_MODE		3
#define	SCC_IVAR_REGSHFT	4
#define	SCC_IVAR_HWMTX		5

/* Hardware class -- the SCC type. */
#define	SCC_CLASS_SAB82532	0
#define	SCC_CLASS_Z8530		1
#define	SCC_CLASS_QUICC		2

/* The possible modes supported by the SCC. */
#define	SCC_MODE_ASYNC		0x01
#define	SCC_MODE_BISYNC		0x02
#define	SCC_MODE_HDLC		0x04

#endif /* _DEV_SCC_BUS_H_ */
