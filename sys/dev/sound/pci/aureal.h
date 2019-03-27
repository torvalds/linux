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

#ifndef _AU8820_REG_H
#define _AU8820_REG_H

#define AU_BUFFSIZE 0x4000

#define AU_REG_FIFOBASE	0x0e000

#define AU_REG_UNK2	0x105c0
#define AU_REG_UNK3	0x10600
#define AU_REG_UNK4	0x10604
#define AU_REG_UNK5	0x10608

#define AU_REG_RTBASE	0x10800

#define AU_REG_ADB	0x10a00

#define AU_REG_CODECCHN	0x11880

#define AU_REG_CODECST	0x11984
#define 	AU_CDC_RUN	0x00000040
#define		AU_CDC_WROK	0x00000100
#define		AU_CDC_RESET	0x00008000

#define AU_REG_CODECIO	0x11988
#define		AU_CDC_DATAMASK	0x0000ffff
#define 	AU_CDC_REGMASK	0x007f0000
#define 	AU_CDC_REGSET	0x00800000
#define		AU_CDC_READY	0x04000000

#define AU_REG_CODECEN	0x11990
#define		AU_CDC_CHAN1EN	0x00000100
#define		AU_CDC_CHAN2EN	0x00000200

#define AU_REG_UNK1	0x1199c

#define AU_REG_IRQSRC	0x12800
#define 	AU_IRQ_FATAL    0x0001
#define 	AU_IRQ_PARITY   0x0002
#define 	AU_IRQ_PCMOUT   0x0020
#define 	AU_IRQ_UNKNOWN  0x1000
#define 	AU_IRQ_MIDI     0x2000
#define AU_REG_IRQEN	0x12804

#define AU_REG_IRQGLOB	0x1280c
#define		AU_IRQ_ENABLE	0x4000

#define AC97_MUTE	0x8000
#define AC97_REG_RESET	0x00
#define AC97_MIX_MASTER	0x02
#define AC97_MIX_PHONES	0x04
#define AC97_MIX_MONO 	0x06
#define AC97_MIX_TONE	0x08
#define AC97_MIX_BEEP	0x0a
#define AC97_MIX_PHONE	0x0c
#define AC97_MIX_MIC	0x0e
#define AC97_MIX_LINE	0x10
#define AC97_MIX_CD	0x12
#define AC97_MIX_VIDEO	0x14
#define AC97_MIX_AUX	0x16
#define	AC97_MIX_PCM	0x18
#define AC97_REG_RECSEL	0x1a
#define AC97_MIX_RGAIN	0x1c
#define AC97_MIX_MGAIN	0x1e
#define AC97_REG_GEN	0x20
#define AC97_REG_3D	0x22
#define AC97_REG_POWER	0x26
#define AC97_REG_ID1	0x7c
#define AC97_REG_ID2	0x7e


#endif
