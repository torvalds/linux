/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Takeshi Shibagaki
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* xe pccard interface driver */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
 
#include <net/ethernet.h>
#include <net/if.h> 
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_mib.h>

#include <dev/xe/if_xereg.h>
#include <dev/xe/if_xevar.h>

#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>

#include "card_if.h"
#include "pccarddevs.h"

/*
 * Debug logging levels - set with hw.xe.debug sysctl
 * 0 = None
 * 1 = More hardware details, probe/attach progress
 * 2 = Most function calls, ioctls and media selection progress
 * 3 = Everything - interrupts, packets in/out and multicast address setup
 */
#define XE_DEBUG
#ifdef XE_DEBUG

extern int xe_debug;

#define	DEVPRINTF(level, arg)	if (xe_debug >= (level)) device_printf arg
#define	DPRINTF(level, arg)	if (xe_debug >= (level)) printf arg
#else
#define	DEVPRINTF(level, arg)
#define	DPRINTF(level, arg)
#endif


#define	XE_CARD_TYPE_FLAGS_NO		0x0
#define	XE_CARD_TYPE_FLAGS_CE2		0x1
#define	XE_CARD_TYPE_FLAGS_MOHAWK	0x2
#define	XE_CARD_TYPE_FLAGS_DINGO	0x4
#define	XE_PROD_ETHER_MASK		0x0100
#define	XE_PROD_MODEM_MASK		0x1000

#define	XE_BOGUS_MAC_OFFSET		0x90

/* MAC vendor prefix used by most Xircom cards is 00:80:c7 */
#define	XE_MAC_ADDR_0			0x00
#define	XE_MAC_ADDR_1			0x80
#define	XE_MAC_ADDR_2			0xc7

/* Some (all?) REM56 cards have vendor prefix 00:10:a4 */
#define	XE_REM56_MAC_ADDR_0		0x00
#define	XE_REM56_MAC_ADDR_1		0x10
#define	XE_REM56_MAC_ADDR_2		0xa4

struct xe_pccard_product {
	struct pccard_product product;
	uint16_t	prodext;
	uint16_t	flags;
};

static const struct xe_pccard_product xe_pccard_products[] = {
	{ PCMCIA_CARD_D(COMPAQ, CPQ550),      0x43, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(COMPAQ2, CPQ_10_100), 0x43, XE_CARD_TYPE_FLAGS_MOHAWK },
	{ PCMCIA_CARD_D(INTEL, EEPRO100),     0x43, XE_CARD_TYPE_FLAGS_MOHAWK },
	{ PCMCIA_CARD_D(INTEL, PRO100LAN56),  0x46, XE_CARD_TYPE_FLAGS_DINGO },
	{ PCMCIA_CARD_D(RACORE, ACCTON_EN2226),0x43, XE_CARD_TYPE_FLAGS_MOHAWK },
	{ PCMCIA_CARD_D(XIRCOM, CE),          0x41, XE_CARD_TYPE_FLAGS_NO },
	{ PCMCIA_CARD_D(XIRCOM, CE2),         0x41, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CE2),         0x42, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CE2_2),       0x41, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CE2_2),       0x42, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CE3),         0x43, XE_CARD_TYPE_FLAGS_MOHAWK },
	{ PCMCIA_CARD_D(XIRCOM, CEM),         0x41, XE_CARD_TYPE_FLAGS_NO },
	{ PCMCIA_CARD_D(XIRCOM, CEM2),        0x42, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CEM28),       0x43, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CEM33),       0x44, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CEM33_2),     0x44, XE_CARD_TYPE_FLAGS_CE2 },
	{ PCMCIA_CARD_D(XIRCOM, CEM56),       0x45, XE_CARD_TYPE_FLAGS_DINGO },
	{ PCMCIA_CARD_D(XIRCOM, CEM56_2),     0x46, XE_CARD_TYPE_FLAGS_DINGO },
	{ PCMCIA_CARD_D(XIRCOM, REM56),       0x46, XE_CARD_TYPE_FLAGS_DINGO },
	{ PCMCIA_CARD_D(XIRCOM, REM10),       0x47, XE_CARD_TYPE_FLAGS_DINGO },
	{ PCMCIA_CARD_D(XIRCOM, XEM5600),     0x56, XE_CARD_TYPE_FLAGS_DINGO },
	{ { NULL }, 0, 0 }	
};

/*
 * Fixing for CEM2, CEM3 and CEM56/REM56 cards.  These need some magic to
 * enable the Ethernet function, which isn't mentioned anywhere in the CIS.
 * Despite the register names, most of this isn't Dingo-specific.
 */
static int
xe_cemfix(device_t dev)
{
	struct xe_softc *sc = device_get_softc(dev);
	int ioport;

	DEVPRINTF(2, (dev, "cemfix\n"));

	DEVPRINTF(1, (dev, "CEM I/O port 0x%0jx, size 0x%0jx\n",
		bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid),
		bus_get_resource_count(dev, SYS_RES_IOPORT, sc->port_rid)));

	pccard_attr_write_1(dev, DINGO_ECOR, DINGO_ECOR_IRQ_LEVEL |
	    DINGO_ECOR_INT_ENABLE | DINGO_ECOR_IOB_ENABLE |
	    DINGO_ECOR_ETH_ENABLE);
	ioport = bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid);
	pccard_attr_write_1(dev, DINGO_EBAR0, ioport & 0xff);
	pccard_attr_write_1(dev, DINGO_EBAR1, (ioport >> 8) & 0xff);

	if (sc->dingo) {
		pccard_attr_write_1(dev, DINGO_DCOR0, DINGO_DCOR0_SF_INT);
		pccard_attr_write_1(dev, DINGO_DCOR1, DINGO_DCOR1_INT_LEVEL |
		    DINGO_DCOR1_EEDIO);
		pccard_attr_write_1(dev, DINGO_DCOR2, 0x00);
		pccard_attr_write_1(dev, DINGO_DCOR3, 0x00);
		pccard_attr_write_1(dev, DINGO_DCOR4, 0x00);
	}
	/* success! */
	return (0);
}

static int
xe_pccard_product_match(device_t dev, const struct pccard_product* ent,
    int vpfmatch)
{
	const struct xe_pccard_product* xpp;
	uint16_t prodext;

	if (vpfmatch == 0)
		return (0);

	xpp = (const struct xe_pccard_product*)ent;
	pccard_get_prodext(dev, &prodext);
	if (xpp->prodext != prodext)
		vpfmatch = 0;
	else
		vpfmatch++;
	return (vpfmatch);
}

static const struct xe_pccard_product *
xe_pccard_get_product(device_t dev)
{
	return ((const struct xe_pccard_product *)pccard_product_lookup(dev,
	    (const struct pccard_product *)xe_pccard_products,
	    sizeof(xe_pccard_products[0]), xe_pccard_product_match));
}

/*
 * Fixing for CE2-class cards with bogus CIS entry for MAC address.
 */
static int
xe_pccard_mac(const struct pccard_tuple *tuple, void *argp)
{
	uint8_t *enaddr = argp, test;
	int i;

	/* Code 0x89 is Xircom special cis node contianing the MAC */
	if (tuple->code != 0x89)
		return (0);

	/* Make sure this is a sane node */
	if (tuple->length != ETHER_ADDR_LEN + 2)
		return (0);
	test = pccard_tuple_read_1(tuple, 0);
	if (test != PCCARD_TPLFE_TYPE_LAN_NID)
		return (0);
	test = pccard_tuple_read_1(tuple, 1);
	if (test != ETHER_ADDR_LEN)
		return (0);

	/* Copy the MAC ADDR and return success */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		enaddr[i] = pccard_tuple_read_1(tuple, i + 2);
	return (1);
}

static int
xe_bad_mac(uint8_t *enaddr)
{
	int i;
	uint8_t sum;

	for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
		sum |= enaddr[i];
	return (sum == 0);
}

/*
 * PCMCIA attach routine.
 * Identify the device.  Called from the bus driver when the card is
 * inserted or otherwise powers up.
 */
static int
xe_pccard_attach(device_t dev)
{
	struct xe_softc *scp = device_get_softc(dev);
	uint32_t vendor,product;
	uint16_t prodext;
	const char* vendor_str = NULL;
	const char* product_str = NULL;
	const char* cis4_str = NULL;
	const char *cis3_str=NULL;
	const struct xe_pccard_product *xpp;
	int err;

	DEVPRINTF(2, (dev, "pccard_attach\n"));

	pccard_get_vendor(dev, &vendor);
	pccard_get_product(dev, &product);
	pccard_get_prodext(dev, &prodext);
	pccard_get_vendor_str(dev, &vendor_str);
	pccard_get_product_str(dev, &product_str);
	pccard_get_cis3_str(dev, &cis3_str);
	pccard_get_cis4_str(dev, &cis4_str);

	DEVPRINTF(1, (dev, "vendor = 0x%04x\n", vendor));
	DEVPRINTF(1, (dev, "product = 0x%04x\n", product));
	DEVPRINTF(1, (dev, "prodext = 0x%02x\n", prodext));
	DEVPRINTF(1, (dev, "vendor_str = %s\n", vendor_str));
	DEVPRINTF(1, (dev, "product_str = %s\n", product_str));
	DEVPRINTF(1, (dev, "cis3_str = %s\n", cis3_str));
	DEVPRINTF(1, (dev, "cis4_str = %s\n", cis4_str));

	xpp = xe_pccard_get_product(dev);
	if (xpp == NULL)
		return (ENXIO);

	/* Set various card ID fields in softc */
	scp->vendor = vendor_str;
	scp->card_type = product_str;
	if (xpp->flags & XE_CARD_TYPE_FLAGS_CE2)
		scp->ce2 = 1;
	if (xpp->flags & XE_CARD_TYPE_FLAGS_MOHAWK)
		scp->mohawk = 1;
	if (xpp->flags & XE_CARD_TYPE_FLAGS_DINGO) {
		scp->dingo = 1;
		scp->mohawk = 1;
	}
	if (xpp->product.pp_product & XE_PROD_MODEM_MASK)
		scp->modem = 1;

	/* Get MAC address */
	pccard_get_ether(dev, scp->enaddr);

	/* Deal with bogus MAC address */
	if (xe_bad_mac(scp->enaddr) &&
	    !pccard_cis_scan(dev, xe_pccard_mac, scp->enaddr)) {
		device_printf(dev,
		    "Unable to find MAC address for your %s card\n",
		    device_get_desc(dev));
		return (ENXIO);
	}

	if ((err = xe_activate(dev)) != 0)
		return (err);
         
	/* Hack RealPorts into submission */
	if (scp->modem && xe_cemfix(dev) < 0) {
		device_printf(dev, "Unable to fix your %s combo card\n",
		    device_get_desc(dev));
		xe_deactivate(dev);
		return (ENXIO);
	}
	if ((err = xe_attach(dev))) {
		device_printf(dev, "xe_attach() failed! (%d)\n", err);
		xe_deactivate(dev);
		return (err);
	}
	return (0);
}

/*
 * The device entry is being removed, probably because someone ejected the
 * card.  The interface should have been brought down manually before calling
 * this function; if not you may well lose packets.  In any case, I shut down
 * the card and the interface, and hope for the best.
 */
static int
xe_pccard_detach(device_t dev)
{
	struct xe_softc *sc = device_get_softc(dev);

	DEVPRINTF(2, (dev, "pccard_detach\n"));

	XE_LOCK(sc);
	xe_stop(sc);
	XE_UNLOCK(sc);
	callout_drain(&sc->media_timer);
	callout_drain(&sc->wdog_timer);
	ether_ifdetach(sc->ifp);
	xe_deactivate(dev);
	mtx_destroy(&sc->lock);
	return (0);
}

static int
xe_pccard_probe(device_t dev)
{
	const struct xe_pccard_product *xpp;

	DEVPRINTF(2, (dev, "pccard_probe\n"));

	/*
	 * Xircom cards aren't proper MFC cards, so we have to take all
	 * cards that match, not just ones that are network.
	 */

	/* If we match something in the table, it is our device. */
	if ((xpp = xe_pccard_get_product(dev)) == NULL)
		return (ENXIO);

	/* Set card name for logging later */
	if (xpp->product.pp_name != NULL)
		device_set_desc(dev, xpp->product.pp_name);

	/* Reject known but unsupported cards */
	if (xpp->flags & XE_CARD_TYPE_FLAGS_NO) {
		device_printf(dev, "Sorry, your %s card is not supported :(\n",
		    device_get_desc(dev));
		return (ENXIO);
	}

	return (0);
}

static device_method_t xe_pccard_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         xe_pccard_probe),
        DEVMETHOD(device_attach,        xe_pccard_attach),
        DEVMETHOD(device_detach,        xe_pccard_detach),

        { 0, 0 }
};

static driver_t xe_pccard_driver = {
        "xe",
        xe_pccard_methods,
        sizeof(struct xe_softc),
};

devclass_t xe_devclass;

DRIVER_MODULE(xe, pccard, xe_pccard_driver, xe_devclass, 0, 0);
PCCARD_PNP_INFO(xe_pccard_products);
