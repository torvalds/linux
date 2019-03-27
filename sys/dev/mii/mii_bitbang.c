/*	$NetBSD: mii_bitbang.c,v 1.12 2008/05/04 17:06:09 xtraeme Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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
 *    notice, this list of conditions and the following didevlaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following didevlaimer in the
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>

MODULE_VERSION(mii_bitbang, 1);

static void mii_bitbang_sendbits(device_t dev, mii_bitbang_ops_t ops,
    uint32_t data, int nbits);

#define	MWRITE(x)							\
do {									\
	ops->mbo_write(dev, (x));					\
	DELAY(1);							\
} while (/* CONSTCOND */ 0)

#define	MREAD		ops->mbo_read(dev)

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
mii_bitbang_sync(device_t dev, mii_bitbang_ops_t ops)
{
	int i;
	uint32_t v;

	v = MDIRPHY | MDO;

	MWRITE(v);
	for (i = 0; i < 32; i++) {
		MWRITE(v | MDC);
		MWRITE(v);
	}
}

/*
 * mii_bitbang_sendbits:
 *
 *	Send a series of bits to the MII.
 */
static void
mii_bitbang_sendbits(device_t dev, mii_bitbang_ops_t ops, uint32_t data,
    int nbits)
{
	int i;
	uint32_t v;

	v = MDIRPHY;
	MWRITE(v);

	for (i = 1 << (nbits - 1); i != 0; i >>= 1) {
		if (data & i)
			v |= MDO;
		else
			v &= ~MDO;
		MWRITE(v);
		MWRITE(v | MDC);
		MWRITE(v);
	}
}

/*
 * mii_bitbang_readreg:
 *
 *	Read a PHY register by bit-bang'ing the MII.
 */
int
mii_bitbang_readreg(device_t dev, mii_bitbang_ops_t ops, int phy, int reg)
{
	int i, error, val;

	mii_bitbang_sync(dev, ops);

	mii_bitbang_sendbits(dev, ops, MII_COMMAND_START, 2);
	mii_bitbang_sendbits(dev, ops, MII_COMMAND_READ, 2);
	mii_bitbang_sendbits(dev, ops, phy, 5);
	mii_bitbang_sendbits(dev, ops, reg, 5);

	/* Switch direction to PHY->host, without a clock transition. */
	MWRITE(MDIRHOST);

	/* Turnaround clock. */
	MWRITE(MDIRHOST | MDC);
	MWRITE(MDIRHOST);

	/* Check for error. */
	error = MREAD & MDI;

	/* Idle clock. */
	MWRITE(MDIRHOST | MDC);
	MWRITE(MDIRHOST);

	val = 0;
	for (i = 0; i < 16; i++) {
		val <<= 1;
		/* Read data prior to clock low-high transition. */
		if (error == 0 && (MREAD & MDI) != 0)
			val |= 1;

		MWRITE(MDIRHOST | MDC);
		MWRITE(MDIRHOST);
	}

	/* Set direction to host->PHY, without a clock transition. */
	MWRITE(MDIRPHY);

	return (error != 0 ? 0 : val);
}

/*
 * mii_bitbang_writereg:
 *
 *	Write a PHY register by bit-bang'ing the MII.
 */
void
mii_bitbang_writereg(device_t dev, mii_bitbang_ops_t ops, int phy, int reg,
    int val)
{

	mii_bitbang_sync(dev, ops);

	mii_bitbang_sendbits(dev, ops, MII_COMMAND_START, 2);
	mii_bitbang_sendbits(dev, ops, MII_COMMAND_WRITE, 2);
	mii_bitbang_sendbits(dev, ops, phy, 5);
	mii_bitbang_sendbits(dev, ops, reg, 5);
	mii_bitbang_sendbits(dev, ops, MII_COMMAND_ACK, 2);
	mii_bitbang_sendbits(dev, ops, val, 16);

	MWRITE(MDIRPHY);
}
