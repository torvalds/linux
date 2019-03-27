/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Stefan Bethke.
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
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <machine/bus.h>
#include <dev/iicbus/iic.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mdio/mdio.h>

#include <dev/etherswitch/etherswitch.h>

#include <dev/etherswitch/arswitch/arswitchreg.h>
#include <dev/etherswitch/arswitch/arswitchvar.h>
#include <dev/etherswitch/arswitch/arswitch_reg.h>

#include "mdio_if.h"
#include "miibus_if.h"
#include "etherswitch_if.h"

static inline void
arswitch_split_setpage(device_t dev, uint32_t addr, uint16_t *phy,
    uint16_t *reg)
{
	struct arswitch_softc *sc = device_get_softc(dev);
	uint16_t page;

	page = (addr >> 9) & 0x1ff;
	*phy = (addr >> 6) & 0x7;
	*reg = (addr >> 1) & 0x1f;

	if (sc->page != page) {
		MDIO_WRITEREG(device_get_parent(dev), 0x18, 0, page);
		DELAY(2000);
		sc->page = page;
	}
}

/*
 * Read half a register.  Some of the registers define control bits, and
 * the sequence of half-word accesses matters.  The register addresses
 * are word-even (mod 4).
 */
static inline int
arswitch_readreg16(device_t dev, int addr)
{
	uint16_t phy, reg;

	arswitch_split_setpage(dev, addr, &phy, &reg);
	return (MDIO_READREG(device_get_parent(dev), 0x10 | phy, reg));
}

/*
 * Write half a register.  See above!
 */
static inline int
arswitch_writereg16(device_t dev, int addr, int data)
{
	uint16_t phy, reg;

	arswitch_split_setpage(dev, addr, &phy, &reg);
	return (MDIO_WRITEREG(device_get_parent(dev), 0x10 | phy, reg, data));
}

/*
 * XXX NOTE:
 *
 * This may not work for AR7240 series embedded switches -
 * the per-PHY register space doesn't seem to be exposed.
 *
 * In that instance, it may be required to speak via
 * the internal switch PHY MDIO bus indirection.
 */
void
arswitch_writedbg(device_t dev, int phy, uint16_t dbg_addr,
    uint16_t dbg_data)
{
	(void) MDIO_WRITEREG(device_get_parent(dev), phy,
	    MII_ATH_DBG_ADDR, dbg_addr);
	(void) MDIO_WRITEREG(device_get_parent(dev), phy,
	    MII_ATH_DBG_DATA, dbg_data);
}

void
arswitch_writemmd(device_t dev, int phy, uint16_t dbg_addr,
    uint16_t dbg_data)
{
	(void) MDIO_WRITEREG(device_get_parent(dev), phy,
	    MII_ATH_MMD_ADDR, dbg_addr);
	(void) MDIO_WRITEREG(device_get_parent(dev), phy,
	    MII_ATH_MMD_DATA, dbg_data);
}

static uint32_t
arswitch_reg_read32(device_t dev, int phy, int reg)
{
	uint16_t lo, hi;
	lo = MDIO_READREG(device_get_parent(dev), phy, reg);
	hi = MDIO_READREG(device_get_parent(dev), phy, reg + 1);

	return (hi << 16) | lo;
}

static int
arswitch_reg_write32(device_t dev, int phy, int reg, uint32_t value)
{
	struct arswitch_softc *sc;
	int r;
	uint16_t lo, hi;

	sc = device_get_softc(dev);
	lo = value & 0xffff;
	hi = (uint16_t) (value >> 16);

	if (sc->mii_lo_first) {
		r = MDIO_WRITEREG(device_get_parent(dev),
		    phy, reg, lo);
		r |= MDIO_WRITEREG(device_get_parent(dev),
		    phy, reg + 1, hi);
	} else {
		r = MDIO_WRITEREG(device_get_parent(dev),
		    phy, reg + 1, hi);
		r |= MDIO_WRITEREG(device_get_parent(dev),
		    phy, reg, lo);
	}

	return r;
}

int
arswitch_readreg(device_t dev, int addr)
{
	uint16_t phy, reg;

	arswitch_split_setpage(dev, addr, &phy, &reg);
	return arswitch_reg_read32(dev, 0x10 | phy, reg);
}

int
arswitch_writereg(device_t dev, int addr, int value)
{
	struct arswitch_softc *sc;
	uint16_t phy, reg;

	sc = device_get_softc(dev);

	arswitch_split_setpage(dev, addr, &phy, &reg);
	return (arswitch_reg_write32(dev, 0x10 | phy, reg, value));
}

/*
 * Read/write 16 bit values in the switch register space.
 *
 * Some of the registers are control registers (eg the MDIO
 * data versus control space) and so need to be treated
 * differently.
 */
int
arswitch_readreg_lsb(device_t dev, int addr)
{

	return (arswitch_readreg16(dev, addr));
}

int
arswitch_readreg_msb(device_t dev, int addr)
{

	return (arswitch_readreg16(dev, addr + 2) << 16);
}

int
arswitch_writereg_lsb(device_t dev, int addr, int data)
{

	return (arswitch_writereg16(dev, addr, data & 0xffff));
}

int
arswitch_writereg_msb(device_t dev, int addr, int data)
{

	return (arswitch_writereg16(dev, addr + 2, (data >> 16) & 0xffff));
}

int
arswitch_modifyreg(device_t dev, int addr, int mask, int set)
{
	int value;
	uint16_t phy, reg;

	ARSWITCH_LOCK_ASSERT((struct arswitch_softc *)device_get_softc(dev),
	    MA_OWNED);

	arswitch_split_setpage(dev, addr, &phy, &reg);

	value = arswitch_reg_read32(dev, 0x10 | phy, reg);
	value &= ~mask;
	value |= set;
	return (arswitch_reg_write32(dev, 0x10 | phy, reg, value));
}

int
arswitch_waitreg(device_t dev, int addr, int mask, int val, int timeout)
{
	struct arswitch_softc *sc = device_get_softc(dev);
	int err, v;
	uint16_t phy, reg;

	ARSWITCH_LOCK_ASSERT(sc, MA_OWNED);

	arswitch_split_setpage(dev, addr, &phy, &reg);

	err = -1;
	while (1) {
		v = arswitch_reg_read32(dev, 0x10 | phy, reg);
		v &= mask;
		if (v == val) {
			err = 0;
			break;
		}
		if (!timeout)
			break;
		DELAY(1);
		timeout--;
	}
	if (err != 0) {
		DPRINTF(sc, ARSWITCH_DBG_ANY,
		    "%s: waitreg failed; addr=0x%08x, mask=0x%08x, val=0x%08x\n",
		    __func__, addr, mask, val);
	}
	return (err);
}
