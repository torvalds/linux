/*	$OpenBSD: nec86reg.h,v 1.1 2014/12/28 13:03:18 aoyama Exp $	*/
/*	$NecBSD: nec86reg.h,v 1.5 1998/03/14 07:04:56 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 */

/*
 * nec86reg.h
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 */

#ifndef	_NEC86REG_H_
#define	_NEC86REG_H_

#define NEC86_PORT		16

/*
 * Offsets from PCM I/O base address
 */
#define NEC86_SOUND_ID		0

#define	NEC86_COREOFFSET	6	/* core chip base */
#define	NEC86_CORESIZE		7
#define NEC86_FIFOSTAT		0	/* readonly */
#define NEC86_VOLUME		0	/* writeonly */
#define NEC86_FIFOCTL		2
#define NEC86_CTRL		4	/* when FIFO not running */
#define NEC86_FIFOINTRBLK	4	/* when FIFO running, writeonly */
#define NEC86_FIFODATA		6

/*
 * FIFO status bits
 */
#define NEC86_FIFOSTAT_FULL	0x80
#define NEC86_FIFOSTAT_EMPTY	0x40
#define NEC86_FIFOSTAT_OVERFLOW	0x20
#define NEC86_FIFOSTAT_LRCLK	0x01

/*
 * FIFO control bits
 */
#define NEC86_FIFOCTL_RUN	0x80
#define NEC86_FIFOCTL_RECMODE	0x40
#define NEC86_FIFOCTL_ENBLINTR	0x20
#define NEC86_FIFOCTL_INTRFLG	0x10
#define NEC86_FIFOCTL_INIT	0x08

#define NEC86_FIFOCTL_MASK_RATE 0x07
#define NEC86_NRATE		8

/*
 * Card control bits
 */
#define NEC86_CTRL_8BITS	0x40
#define NEC86_CTRL_PAN_L	0x20
#define NEC86_CTRL_PAN_R	0x10

#define NEC86_CTRL_MASK_PAN	0x30
#define NEC86_CTRL_MASK_PORT	0x0f

#define NEC86_CTRL_PORT_STD	0x02	/* PC-9801-86 supports only this */

/*
 * Volume
 */
#define NEC86_VOLUME_MASK_PORT	0xe0
#define NEC86_VOLUME_MASK_VOL	0x0f

#define NEC86_VOLUME_PORT_OPNAD	0	/* OPNA -> LINEOUT */
#define NEC86_VOLUME_PORT_OPNAI	1	/* OPNA -> ADC */
#define NEC86_VOLUME_PORT_LINED 2	/* LINEIN (& CDDA) -> LINEOUT */
#define NEC86_VOLUME_PORT_LINEI 3	/* LINEIN (& CDDA) -> ADC */
#define NEC86_VOLUME_PORT_CDDAD 4	/* CDDA -> LINEOUT (PC-98GS only) */
#define NEC86_VOLUME_PORT_PCMD	5	/* DAC -> LINEOUT */

#define NEC86_MINVOL		0
#define NEC86_MAXVOL		15

#define NEC86_VOL_TO_BITS(p, v)	(((p) << 5) | ((v) ^ 0xf))

/*
 * Unit size of the interrupt watermark (NEC86_FIFOINTRBLK).
 */
#define NEC86_INTRBLK_UNIT	128	/* 128 bytes */

/*
 * Size of the FIFO ring buffer on the board.
 *
 * XXX -  We define this value a little less than the actual size to work
 *	  around the bug at 'buffer full' in Q-Vision's cards.
 *	  ... Too paranoid?
 */
#define NEC86_BUFFSIZE	(32768 - 2 * 2)	/* 2(stereo) * 2(16bits sampling) */

/*
 * I/O base addresses for YAMAHA FM sound chip OPNA (YM2608/YMF288)
 */
#define OPNA_IOBASE0	0x088
#define OPNA_IOBASE1	0x188
#define OPNA_IOBASE2	0x288
#define OPNA_IOBASE3	0x388

/*
 * Macros to detect valid hardware configuration data
 */
#define NEC86_IRQ_VALID(irq)	(((irq) == 3) || ((irq) == 10) || \
				 ((irq) == 12) || ((irq) == 13))
#define NEC86_BASE_VALID(base)	(((base) == 0xa460) || ((base) == 0xa470) || \
				 ((base) == 0xa480) || ((base) == 0xa490))
#endif	/* !_NEC86REG_H_ */
