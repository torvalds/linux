/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_POWERPC_VIAREG_H_
#define	_POWERPC_VIAREG_H_

/* VIA interface registers */
#define vBufB		0x0000	/* register B */
#define vDirB		0x0400	/* data direction register */
#define vDirA		0x0600	/* data direction register */
#define vT1C		0x0800	/* Timer 1 counter Lo */
#define vT1CH		0x0a00	/* Timer 1 counter Hi */
#define vSR		0x1400	/* shift register */
#define vACR		0x1600	/* aux control register */
#define vPCR		0x1800	/* peripheral control register */
#define vIFR		0x1a00	/* interrupt flag register */
#define vIER		0x1c00	/* interrupt enable register */
#define vBufA		0x1e00	/* register A */

#define vPB		0x0000
#define vPB3		0x08
#define vPB4		0x10
#define vPB5		0x20
#define vSR_INT		0x04
#define vSR_OUT		0x10

#endif /* _POWERPC_VIAREG_H_ */
