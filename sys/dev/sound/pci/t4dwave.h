/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
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

#ifndef _T4DWAVE_REG_H
#define _T4DWAVE_REG_H

#define TR_REG_CIR	0xa0
#define 	TR_CIR_MASK	0x0000003f
#define		TR_CIR_ADDRENA	0x00001000
#define		TR_CIR_MIDENA	0x00002000
#define TR_REG_MISCINT	0xb0
#define		TR_INT_ADDR	0x00000020
#define		TR_INT_SB	0x00000004

#define TR_REG_DMAR0	0x00
#define TR_REG_DMAR4	0x04
#define TR_REG_DMAR11	0x0b
#define TR_REG_DMAR15	0x0f
#define TR_REG_SBR4	0x14
#define TR_REG_SBR5	0x15
#define 	TR_SB_INTSTATUS	0x82
#define TR_REG_SBR9	0x1e
#define TR_REG_SBR10	0x1f
#define TR_REG_SBBL	0xc0
#define TR_REG_SBCTRL	0xc4
#define TR_REG_SBDELTA	0xac

#define TR_CDC_DATA	16
#define TDX_REG_CODECWR	0x40
#define TDX_REG_CODECRD	0x44
#define 	TDX_CDC_RWSTAT	0x00008000
#define TDX_REG_CODECST	0x48
#define		TDX_CDC_SBCTRL	0x40
#define		TDX_CDC_ACTIVE	0x20
#define		TDX_CDC_READY	0x10
#define		TDX_CDC_ADCON	0x08
#define		TDX_CDC_DACON	0x02
#define		TDX_CDC_RESET	0x01
#define		TDX_CDC_ON	(TDX_CDC_ADCON|TDX_CDC_DACON)

#define SPA_REG_CODECRD	0x44
#define SPA_REG_CODECWR	0x40
#define SPA_REG_CODECST	0x48
#define SPA_RST_OFF	0x0f0000
#define SPA_REG_GPIO	0x48
#define SPA_CDC_RWSTAT	0x00008000

#define TNX_REG_CODECWR	0x44
#define TNX_REG_CODEC1RD 0x48
#define TNX_REG_CODEC2RD 0x4c
#define 	TNX_CDC_RWSTAT	0x00000c00
#define		TNX_CDC_SEC	0x00000100
#define TNX_REG_CODECST	0x40
#define		TNX_CDC_READY2	0x40
#define		TNX_CDC_ADC2ON	0x20
#define		TNX_CDC_DAC2ON	0x10
#define		TNX_CDC_READY1	0x08
#define		TNX_CDC_ADC1ON	0x04
#define		TNX_CDC_DAC1ON	0x02
#define		TNX_CDC_RESET	0x01
#define		TNX_CDC_ON	(TNX_CDC_ADC1ON|TNX_CDC_DAC1ON)


#define	TR_REG_STARTA	0x80
#define TR_REG_STOPA	0x84
#define	TR_REG_CSPF_A	0x90
#define TR_REG_ADDRINTA	0x98
#define TR_REG_INTENA	0xa4

#define	TR_REG_STARTB	0xb4
#define TR_REG_STOPB	0xb8
#define	TR_REG_CSPF_B	0xbc
#define TR_REG_ADDRINTB	0xd8
#define TR_REG_INTENB	0xdc

#define TR_REG_CHNBASE	0xe0
#define TR_CHN_REGS	5

#endif
