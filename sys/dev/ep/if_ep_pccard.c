/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@novatel.ca>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
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
 */

/*
 * Pccard support for 3C589 by:
 *		HAMADA Naoki
 *		nao@tom-yam.or.jp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <dev/ep/if_epreg.h>
#include <dev/ep/if_epvar.h>

#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>

#include "pccarddevs.h"

struct ep_pccard_product
{
	struct pccard_product prod;
	int chipset;
};

#define EP_CHIP_589	1	/* Classic 3c5x9 chipset */
#define EP_CHIP_574	2	/* Roadrunner */
#define EP_CHIP_C1	3	/* 3c1 */

static const struct ep_pccard_product ep_pccard_products[] = {
	{ PCMCIA_CARD(3COM, 3C1),		EP_CHIP_C1 },
	{ PCMCIA_CARD(3COM, 3C562),		EP_CHIP_589 },
	{ PCMCIA_CARD(3COM, 3C589),		EP_CHIP_589 },
	{ PCMCIA_CARD(3COM, 3CXEM556),		EP_CHIP_589 },
	{ PCMCIA_CARD(3COM, 3CXEM556INT),	EP_CHIP_589 },
	{ PCMCIA_CARD(3COM, 3C574),		EP_CHIP_574 },
	{ PCMCIA_CARD(3COM, 3CCFEM556BI),	EP_CHIP_574 },
	{ { NULL } }
};

static const struct ep_pccard_product *
ep_pccard_lookup(device_t dev)
{
	return ((const struct ep_pccard_product *)pccard_product_lookup(dev,
	    (const struct pccard_product *)ep_pccard_products,
	    sizeof(ep_pccard_products[0]), NULL));
}

static int
ep_pccard_probe(device_t dev)
{
	const struct ep_pccard_product *pp;
	int		error;
	uint32_t	fcn = PCCARD_FUNCTION_UNSPEC;

	/* Make sure we're a network function */
	error = pccard_get_function(dev, &fcn);
	if (error != 0)
		return (error);
	if (fcn != PCCARD_FUNCTION_NETWORK)
		return (ENXIO);

	/* Check to see if we know about this card */
	if ((pp = ep_pccard_lookup(dev)) == NULL)
		return EIO;
	if (pp->prod.pp_name != NULL)
		device_set_desc(dev, pp->prod.pp_name);

	return 0;
}

static int
ep_pccard_mac(const struct pccard_tuple *tuple, void *argp)
{
	uint8_t *enaddr = argp;
	int i;

	/* Code 0x88 is 3com's special cis node contianing the MAC */
	if (tuple->code != 0x88)
		return (0);

	/* Make sure this is a sane node */
	if (tuple->length < ETHER_ADDR_LEN)
		return (0);

	/* Copy the MAC ADDR and return success */
	for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
		enaddr[i] = pccard_tuple_read_1(tuple, i + 1);
		enaddr[i + 1] = pccard_tuple_read_1(tuple, i);
	}
	return (1);
}

static int
ep_pccard_attach(device_t dev)
{
	struct ep_softc *sc = device_get_softc(dev);
	uint16_t result;
	int error = 0;
	const struct ep_pccard_product *pp;

	if ((pp = ep_pccard_lookup(dev)) == NULL)
		panic("ep_pccard_attach: can't find product in attach.");

	if (pp->chipset == EP_CHIP_574) {
		sc->epb.mii_trans = 1;
		sc->epb.cmd_off = 2;
	} else {
		sc->epb.mii_trans = 0;
		sc->epb.cmd_off = 0;
	}
	if ((error = ep_alloc(dev))) {
		device_printf(dev, "ep_alloc() failed! (%d)\n", error);
		goto bad;
	}

	if (pp->chipset == EP_CHIP_C1)
		sc->stat |= F_HAS_TX_PLL;
	
	/* ROM size = 0, ROM base = 0 */
	/* For now, ignore AUTO SELECT feature of 3C589B and later. */
	error = ep_get_e(sc, EEPROM_ADDR_CFG, &result);
	CSR_WRITE_2(sc, EP_W0_ADDRESS_CFG, result & 0xc000);

	/* 
	 * Fake IRQ must be 3 for 3C589 and 3C589B.  3C589D and newer
	 * ignore this value.  3C589C is unknown, as are the other
	 * cards supported by this driver, but it appears to never hurt
	 * and always helps.
	 */
	SET_IRQ(sc, 3);
	CSR_WRITE_2(sc, EP_W0_PRODUCT_ID, sc->epb.prod_id);

	if (sc->epb.mii_trans) {
		/*
		 * turn on the MII transciever
		 */
		GO_WINDOW(sc, 3);
		CSR_WRITE_2(sc, EP_W3_OPTIONS, 0x8040);
		DELAY(1000);
		CSR_WRITE_2(sc, EP_W3_OPTIONS, 0xc040);
		CSR_WRITE_2(sc, EP_COMMAND, RX_RESET);
		CSR_WRITE_2(sc, EP_COMMAND, TX_RESET);
		EP_BUSY_WAIT(sc);
		DELAY(1000);
		CSR_WRITE_2(sc, EP_W3_OPTIONS, 0x8040);
	} else
		ep_get_media(sc);

	/*
	 * The 3C562 (a-c revisions) stores the MAC in the CIS in a
	 * way that's unique to 3com.  If we have one of these cards,
	 * scan the CIS for that MAC address, and use it if we find
	 * it.  The NetBSD driver says that the ROADRUNNER chips also
	 * do this, which may be true, but none of the cards that I
	 * have include this TUPLE.  Always prefer the MAC addr in the
	 * CIS tuple to the one returned by the card, as it appears that
	 * only those cards that need it have this special tuple.
	 */
	if (pccard_cis_scan(dev, ep_pccard_mac, sc->eaddr))
		sc->stat |= F_ENADDR_SKIP;
	if ((error = ep_attach(sc))) {
		device_printf(dev, "ep_attach() failed! (%d)\n", error);
		goto bad;
	}
	if ((error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, ep_intr, sc, &sc->ep_intrhand))) {
		device_printf(dev, "bus_setup_intr() failed! (%d)\n", error);
		goto bad;
	}
	return (0);
bad:
	ep_free(dev);
	return (error);
}

static device_method_t ep_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ep_pccard_probe),
	DEVMETHOD(device_attach, ep_pccard_attach),
	DEVMETHOD(device_detach, ep_detach),

	DEVMETHOD_END
};

static driver_t ep_pccard_driver = {
	"ep",
	ep_pccard_methods,
	sizeof(struct ep_softc),
};

extern devclass_t ep_devclass;

DRIVER_MODULE(ep, pccard, ep_pccard_driver, ep_devclass, 0, 0);
PCCARD_PNP_INFO(ep_pccard_products);
