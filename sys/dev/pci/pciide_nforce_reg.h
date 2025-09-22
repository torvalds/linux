/*	$OpenBSD: pciide_nforce_reg.h,v 1.3 2004/09/24 07:38:38 grange Exp $	*/

/*
 * Copyright (c) 2003 Alexander Yurchenko <grange@openbsd.org>
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_PCIIDE_NFORCE_REG_H_
#define _DEV_PCI_PCIIDE_NFORCE_REG_H_

/* Configuration register */
#define NFORCE_CONF		0x50
#define NFORCE_CHAN_EN(chan) \
	(0x00000001 << (1 - (chan)))

/* PIO and multiword DMA timing register */
#define NFORCE_PIODMATIM	0x58
#define NFORCE_PIODMATIM_MASK(chan) \
	(0xffff << ((1 - (chan)) * 16))
#define NFORCE_PIODMATIM_SET(chan, drive, x) \
	((x) << ((3 - ((chan) * 2 + (drive))) * 8))

/* PIO timing register */
#define NFORCE_PIOTIM		0x5c

/* UDMA timing register */
#define NFORCE_UDMATIM		0x60
#define NFORCE_UDMATIM_MASK(chan) \
	(0xffff << ((1 - (chan)) * 16))
#define NFORCE_UDMATIM_SET(chan, drive, x) \
	((x) << ((3 - ((chan) * 2 + (drive))) * 8))
#define NFORCE_UDMA_EN(chan, drive) \
	(0x40 << ((3 - ((chan) * 2 + (drive))) * 8))
#define NFORCE_UDMA_ENM(chan, drive) \
	(0x80 << ((3 - ((chan) * 2 + (drive))) * 8))

/* Timing values */
static u_int8_t nforce_pio[] = { 0xa8, 0x65, 0x42, 0x22, 0x20 };
static u_int8_t nforce_udma[] = { 0x02, 0x01, 0x00, 0x04, 0x05, 0x06, 0x07 };

#endif	/* !_DEV_PCI_PCIIDE_NFORCE_REG_H_ */
