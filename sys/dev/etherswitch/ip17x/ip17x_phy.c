/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Luiz Otavio O Souza.
 * Copyright (c) 2011-2012 Stefan Bethke.
 * Copyright (c) 2012 Adrian Chadd.
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>

#include <dev/mii/mii.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/etherswitch/ip17x/ip17x_phy.h>
#include <dev/etherswitch/ip17x/ip17x_reg.h>
#include <dev/etherswitch/ip17x/ip17x_var.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

int
ip17x_readphy(device_t dev, int phy, int reg)
{
	struct ip17x_softc *sc;
	int data;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	IP17X_LOCK(sc);
	data = MDIO_READREG(device_get_parent(dev), phy, reg);
	IP17X_UNLOCK(sc);

	return (data);
}

int
ip17x_writephy(device_t dev, int phy, int reg, int data)
{
	struct ip17x_softc *sc;
	int err;

	sc = device_get_softc(dev);
	IP17X_LOCK_ASSERT(sc, MA_NOTOWNED);

	if (phy < 0 || phy >= 32)
		return (ENXIO);
	if (reg < 0 || reg >= 32)
		return (ENXIO);

	IP17X_LOCK(sc);
	err = MDIO_WRITEREG(device_get_parent(dev), phy, reg, data);
	IP17X_UNLOCK(sc);

	return (err);
}

int
ip17x_updatephy(device_t dev, int phy, int reg, int mask, int value)
{
	int val;

	val = ip17x_readphy(dev, phy, reg);
	val &= ~mask;
	val |= value;
	return (ip17x_writephy(dev, phy, reg, val));
}
