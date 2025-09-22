/*	$OpenBSD: mii_bitbang.c,v 1.5 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD: mii_bitbang.c,v 1.6 2004/08/23 06:18:39 thorpej Exp $	*/

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

/*
 * Common module for bit-bang'ing the MII.
 */

#include <sys/param.h>
#include <sys/device.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>

void	mii_bitbang_sync(struct device *, mii_bitbang_ops_t);
void	mii_bitbang_sendbits(struct device *, mii_bitbang_ops_t,
	    u_int32_t, int);

#define	WRITE(x)							\
do {									\
	ops->mbo_write(sc, (x));					\
	delay(1);							\
} while (0)

#define	READ		ops->mbo_read(sc)

#define	MDO		ops->mbo_bits[MII_BIT_MDO]
#define	MDI		ops->mbo_bits[MII_BIT_MDI]
#define	MDC		ops->mbo_bits[MII_BIT_MDC]
#define	MDIRPHY		ops->mbo_bits[MII_BIT_DIR_HOST_PHY]
#define	MDIRHOST	ops->mbo_bits[MII_BIT_DIR_PHY_HOST]

/*
 * mii_bitbang_sync:
 *
 *	Synchronize the MII.
 */
void
mii_bitbang_sync(struct device *sc, mii_bitbang_ops_t ops)
{
	int i;
	u_int32_t v;

	v = MDIRPHY | MDO;

	WRITE(v);
	for (i = 0; i < 32; i++) {
		WRITE(v | MDC);
		WRITE(v);
	}
}

/*
 * mii_bitbang_sendbits:
 *
 *	Send a series of bits to the MII.
 */
void
mii_bitbang_sendbits(struct device *sc, mii_bitbang_ops_t ops,
    u_int32_t data, int nbits)
{
	int i;
	u_int32_t v;

	v = MDIRPHY;
	WRITE(v);

	for (i = 1 << (nbits - 1); i != 0; i >>= 1) {
		if (data & i)
			v |= MDO;
		else
			v &= ~MDO;
		WRITE(v);
		WRITE(v | MDC);
		WRITE(v);
	}
}

/*
 * mii_bitbang_readreg:
 *
 *	Read a PHY register by bit-bang'ing the MII.
 */
int
mii_bitbang_readreg(struct device *sc, mii_bitbang_ops_t ops, int phy,
    int reg)
{
	int val = 0, err = 0, i;

	mii_bitbang_sync(sc, ops);

	mii_bitbang_sendbits(sc, ops, MII_COMMAND_START, 2);
	mii_bitbang_sendbits(sc, ops, MII_COMMAND_READ, 2);
	mii_bitbang_sendbits(sc, ops, phy, 5);
	mii_bitbang_sendbits(sc, ops, reg, 5);

	/* Switch direction to PHY->host, without a clock transition. */
	WRITE(MDIRHOST);

	/* Turnaround clock. */
	WRITE(MDIRHOST | MDC);
	WRITE(MDIRHOST);

	/* Check for error. */
	err = READ & MDI;

	/* Idle clock. */
	WRITE(MDIRHOST | MDC);
	WRITE(MDIRHOST);

	for (i = 0; i < 16; i++) {
		val <<= 1;
		/* Read data prior to clock low-high transition. */
		if (err == 0 && (READ & MDI) != 0)
			val |= 1;

		WRITE(MDIRHOST | MDC);
		WRITE(MDIRHOST);
	}

	/* Set direction to host->PHY, without a clock transition. */
	WRITE(MDIRPHY);

	return (err ? 0 : val);
}

/*
 * mii_bitbang_writereg:
 *
 *	Write a PHY register by bit-bang'ing the MII.
 */
void
mii_bitbang_writereg(struct device *sc, mii_bitbang_ops_t ops,
    int phy, int reg, int val)
{

	mii_bitbang_sync(sc, ops);

	mii_bitbang_sendbits(sc, ops, MII_COMMAND_START, 2);
	mii_bitbang_sendbits(sc, ops, MII_COMMAND_WRITE, 2);
	mii_bitbang_sendbits(sc, ops, phy, 5);
	mii_bitbang_sendbits(sc, ops, reg, 5);
	mii_bitbang_sendbits(sc, ops, MII_COMMAND_ACK, 2);
	mii_bitbang_sendbits(sc, ops, val, 16);

	WRITE(MDIRPHY);
}
