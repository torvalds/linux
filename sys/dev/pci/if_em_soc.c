/*	$OpenBSD: if_em_soc.c,v 1.6 2025/06/12 06:43:22 jsg Exp $	*/

/*
 * Copyright (c) 2009 Dariusz Swiderski <sfires@sfires.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <dev/pci/if_em.h>
#include <dev/pci/if_em_hw.h>
#include <dev/pci/if_em_soc.h>
#include <dev/pci/gcu_var.h>
#include <dev/pci/gcu_reg.h>

#include "gcu.h"

void em_media_status(struct ifnet *, struct ifmediareq *);
int em_media_change(struct ifnet *);

void *
em_lookup_gcu(struct device *self)
{
#if NGCU > 0
	extern struct cfdriver gcu_cd;

	return (device_lookup(&gcu_cd, 0));
#else
	return (NULL);
#endif
}

int
em_attach_miibus(struct device *self)
{
	return 0;
}

int
gcu_miibus_readreg(struct em_hw *hw, int phy, int reg)
{
	struct em_softc *sc = (struct em_softc *)
	    ((struct em_osdep *)hw->back)->dev;
	struct gcu_softc *gcu = hw->gcu;
	uint32_t data = 0;
	uint32_t done = 0;
	int i = 0;

	if (gcu == 0)
		return 0;

	/* format the data to be written to MDIO_COMMAND_REG */
	data |= (reg << MDIO_COMMAND_PHY_REG_OFFSET);
	data |= (phy << MDIO_COMMAND_PHY_ADDR_OFFSET);
	data |= MDIO_COMMAND_GO_MASK;

	mtx_enter(&gcu->mdio_mtx);
	bus_space_write_4(gcu->tag, gcu->handle, MDIO_COMMAND_REG, data);

	while (!done && (i++ < GCU_MAX_ATTEMPTS)) {
		DELAY(GCU_CMD_DELAY);
		data = bus_space_read_4(gcu->tag, gcu->handle, 
		    MDIO_COMMAND_REG);
		done = !((data & MDIO_COMMAND_GO_MASK) >> 
		    MDIO_COMMAND_GO_OFFSET);
	}
	mtx_leave(&gcu->mdio_mtx);

	if (i >= GCU_MAX_ATTEMPTS) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    DEVNAME(sc), phy, reg);
		return (0);
	}

	mtx_enter(&gcu->mdio_mtx);
	data = bus_space_read_4(gcu->tag, gcu->handle, MDIO_STATUS_REG);
	mtx_leave(&gcu->mdio_mtx);

	if((data & MDIO_STATUS_STATUS_MASK) != 0) {
		printf("%s: unable to read phy %d reg %d\n",
		    DEVNAME(sc), phy, reg);
		return (0);
	}
	return (uint16_t) (data & MDIO_STATUS_READ_DATA_MASK);
}

void
gcu_miibus_writereg(struct em_hw *hw, int phy, int reg, int val)
{
	struct em_softc *sc = (struct em_softc *)
	    ((struct em_osdep *)hw->back)->dev;
	struct gcu_softc *gcu = hw->gcu;
	uint32_t data, done = 0;
	int i = 0;

	if (gcu == 0)
		return;

	/* format the data to be written to the MDIO_COMMAND_REG */
	data = val;
	data |= (reg << MDIO_COMMAND_PHY_REG_OFFSET);
	data |= (phy << MDIO_COMMAND_PHY_ADDR_OFFSET);
	data |= MDIO_COMMAND_OPER_MASK | MDIO_COMMAND_GO_MASK;

	mtx_enter(&gcu->mdio_mtx);
	bus_space_write_4(gcu->tag, gcu->handle, MDIO_COMMAND_REG, data);

	while (!done && (i++ < GCU_MAX_ATTEMPTS)) {
		DELAY(GCU_CMD_DELAY);
		data = bus_space_read_4(gcu->tag, gcu->handle, 
		    MDIO_COMMAND_REG);
		done = !((data & MDIO_COMMAND_GO_MASK) >> 
		    MDIO_COMMAND_GO_OFFSET);
	}
	mtx_leave(&gcu->mdio_mtx);

	if (i >= GCU_MAX_ATTEMPTS) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    DEVNAME(sc), phy, reg);
		return;
	}
}
