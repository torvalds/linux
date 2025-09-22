/* 	$OpenBSD: pciide_ixp_reg.h,v 1.2 2008/06/26 05:42:17 ray Exp $	*/
/* $NetBSD: pciide_ixp_reg.h,v 1.2 2005/02/27 00:27:33 perry Exp $ */

/*
 *  Copyright (c) 2004 The NetBSD Foundation.
 *  All rights reserved.
 *
 *  This code is derived from software contributed to the NetBSD Foundation
 *  by Quentin Garnier.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 *  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/* All values gathered from the linux driver. */

#define IXP_PIO_TIMING	0x40
#define IXP_MDMA_TIMING	0x44
#define IXP_PIO_CTL	0x48
#define IXP_PIO_MODE	0x4a
#define IXP_UDMA_CTL	0x54
#define IXP_UDMA_MODE	0x56

static const uint8_t ixp_pio_timings[] = {
	0x5d, 0x47, 0x34, 0x22, 0x20
};

static const uint8_t ixp_mdma_timings[] = {
	0x77, 0x21, 0x20
};

/* First 4 bits of UDMA_CTL enable or disable UDMA for the drive */
#define IXP_UDMA_ENABLE(u, c, d)	do {	\
    	(u) |= (1 << (2 * (c) + (d)));		\
    } while (0)
#define IXP_UDMA_DISABLE(u, c, d)	do {	\
    	(u) &= ~(1 << (2 * (c) + (d)));		\
    } while (0)

/*
 * UDMA_MODE has 4 bits per drive, though only 3 are actually used
 * Note that in this macro u is the whole
 * UDMA_CTL+UDMA_MODE register (32bits).
 * PIO_MODE works just the same.
 */
#define IXP_SET_MODE(u, c, d, m)	do {	\
    	int __ixpshift = 16 + 8*(c) + 4*(d);	\
    	(u) &= ~(0x7 << __ixpshift);		\
    	(u) |= (((m) & 0x7) << __ixpshift);	\
    } while (0)

/*
 * MDMA_TIMING has one byte per drive.
 * PIO_TIMING works just the same.
 */
#define IXP_SET_TIMING(m, c, d, t)	do {	\
        int __ixpshift = 16*(c) + 8*(d);	\
    	(m) &= ~(0xff << __ixpshift);		\
    	(m) |= ((t) & 0xff) << __ixpshift;	\
    } while (0)
