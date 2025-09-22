/*	$OpenBSD: pciide_acard_reg.h,v 1.5 2004/09/24 07:43:03 grange Exp $	*/
/*	$NetBSD: pciide_acard_reg.h,v 1.1 2001/04/21 16:36:38 tsutsui Exp $	*/

/*
 * Copyright (c) 2001 Izumi Tsutsui.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#ifndef _DEV_PCI_PCIIDE_ACARD_REG_H_
#define _DEV_PCI_PCIIDE_ACARD_REG_H_

#define ATP850_IDETIME(channel)	(0x40 + (channel) * 4)
#define ATP860_IDETIME		0x40

#define	ATP850_SETTIME(drive, act, rec)					\
	    (((((act) & 0xf) << 8) | ((rec) & 0xf)) << ((drive) * 16))
#define	ATP860_SETTIME(channel, drive, act, rec)			\
	    (((((act) & 0xf) << 4) | ((rec) & 0xf)) <<			\
	    ((channel) * 16 + (drive) * 8))
#define	ATP860_SETTIME_MASK(channel)	(0xffff << ((channel) * 16))

static const u_int8_t acard_act_udma[] = {0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3};
static const u_int8_t acard_rec_udma[] = {0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1};
static const u_int8_t acard_act_dma[]  = {0x0, 0x3, 0x3};
static const u_int8_t acard_rec_dma[]  = {0xa, 0x3, 0x1};
static const u_int8_t acard_act_pio[]  = {0x0, 0x0, 0x0, 0x3, 0x3};
static const u_int8_t acard_rec_pio[]  = {0x0, 0xa, 0x8, 0x3, 0x1};

#define ATP850_UDMA		0x54
#define ATP860_UDMA		0x44

#define	ATP850_UDMA_MODE(channel, drive, x)		\
	    (((x) & 0x3) << ((channel) * 4 + (drive) * 2))
#define	ATP860_UDMA_MODE(channel, drive, x)		\
	    (((x) & 0xf) << ((channel) * 8 + (drive) * 4))
#define	ATP850_UDMA_MASK(channel)	(0xf << ((channel) * 4))
#define	ATP860_UDMA_MASK(channel)	(0xff << ((channel) * 8))

static const u_int8_t acard_udma_conf[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};

#define ATP8x0_CTRL		0x48
#define  ATP8x0_CTRL_EN(chan)	(0x00020000 << (chan))
#define  ATP860_CTRL_INT	0x00010000
#define  ATP860_CTRL_80P(chan)	(0x00000100 << (chan))

#endif	/* !_DEV_PCI_PCIIDE_ACARD_REG_H_ */
