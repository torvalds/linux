/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2017 Manuel Stuehn
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _TI_PRUSS_H_
#define _TI_PRUSS_H_

#define	PRUSS_AM18XX_INTC	0x04000
#define	PRUSS_AM18XX_REV	0x4e825900
#define	PRUSS_AM33XX_REV	0x4e82A900
#define	PRUSS_AM33XX_INTC	0x20000

#define PRUSS_INTC_GER			(PRUSS_AM33XX_INTC + 0x0010)
#define PRUSS_INTC_SISR			(PRUSS_AM33XX_INTC + 0x0020)
#define PRUSS_INTC_SICR			(PRUSS_AM33XX_INTC + 0x0024)
#define PRUSS_INTC_EISR			(PRUSS_AM33XX_INTC + 0x0028)
#define PRUSS_INTC_EICR			(PRUSS_AM33XX_INTC + 0x002C)
#define PRUSS_INTC_HIEISR		(PRUSS_AM33XX_INTC + 0x0034)
#define PRUSS_INTC_HIDISR		(PRUSS_AM33XX_INTC + 0x0038)
#define PRUSS_INTC_SECR0		(PRUSS_AM33XX_INTC + 0x0280)
#define PRUSS_INTC_SECR1		(PRUSS_AM33XX_INTC + 0x0284)
#define PRUSS_INTC_CMR_BASE		(PRUSS_AM33XX_INTC + 0x0400)
#define PRUSS_INTC_HMR_BASE		(PRUSS_AM33XX_INTC + 0x0800)
#define PRUSS_INTC_HIPIR_BASE	(PRUSS_AM33XX_INTC + 0x0900)
#define PRUSS_INTC_SIPR0		(PRUSS_AM33XX_INTC + 0x0D00)
#define PRUSS_INTC_SIPR1		(PRUSS_AM33XX_INTC + 0x0D04)
#define PRUSS_INTC_SITR0		(PRUSS_AM33XX_INTC + 0x0D80)
#define PRUSS_INTC_SITR1		(PRUSS_AM33XX_INTC + 0x0D84)
#define PRUSS_INTC_HIER		    (PRUSS_AM33XX_INTC + 0x1500)

#endif /* _TI_PRUSS_H_ */
