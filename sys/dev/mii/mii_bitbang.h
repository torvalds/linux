/*	$OpenBSD: mii_bitbang.h,v 1.3 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	MII_BIT_MDO		0	/* data out (host->PHY) */
#define	MII_BIT_MDI		1	/* data in (PHY->host) */
#define	MII_BIT_MDC		2	/* clock */
#define	MII_BIT_DIR_HOST_PHY	3	/* set direction: host->PHY */
#define	MII_BIT_DIR_PHY_HOST	4	/* set direction: PHY->host */
#define	MII_NBITS		5

struct mii_bitbang_ops {
	u_int32_t	(*mbo_read)(struct device *);
	void		(*mbo_write)(struct device *, u_int32_t);
	u_int32_t	mbo_bits[MII_NBITS];
};

typedef	const struct mii_bitbang_ops *mii_bitbang_ops_t;

int	mii_bitbang_readreg(struct device *, mii_bitbang_ops_t,
	    int, int);
void	mii_bitbang_writereg(struct device *, mii_bitbang_ops_t,
	    int, int, int);
