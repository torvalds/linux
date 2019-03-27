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
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <isa/isavar.h>

#include <dev/ep/if_epreg.h>
#include <dev/ep/if_epvar.h>

#ifdef __i386__
#include <dev/ep/elink.h>
#endif

#ifdef __i386__
static uint16_t get_eeprom_data(int, int);
static void ep_isa_identify(driver_t *, device_t);
#endif

static int ep_isa_probe(device_t);
static int ep_isa_attach(device_t);
static int ep_eeprom_cksum(struct ep_softc *);

struct isa_ident {
	uint32_t id;
	char *name;
};
const char *ep_isa_match_id(uint32_t, struct isa_ident *);

#define ISA_ID_3C509_XXX   0x0506d509
#define ISA_ID_3C509_TP    0x506d5090
#define ISA_ID_3C509_BNC   0x506d5091
#define ISA_ID_3C509_COMBO 0x506d5094
#define ISA_ID_3C509_TPO   0x506d5095
#define ISA_ID_3C509_TPC   0x506d5098

#ifdef __i386__
static struct isa_ident ep_isa_devs[] = {
	{ISA_ID_3C509_TP, "3Com 3C509-TP EtherLink III"},
	{ISA_ID_3C509_BNC, "3Com 3C509-BNC EtherLink III"},
	{ISA_ID_3C509_COMBO, "3Com 3C509-Combo EtherLink III"},
	{ISA_ID_3C509_TPO, "3Com 3C509-TPO EtherLink III"},
	{ISA_ID_3C509_TPC, "3Com 3C509-TPC EtherLink III"},
	{0, NULL},
};
#endif

static struct isa_pnp_id ep_ids[] = {
	{0x90506d50, "3Com 3C509B-TP EtherLink III (PnP)"},	/* TCM5090 */
	{0x91506d50, "3Com 3C509B-BNC EtherLink III (PnP)"},	/* TCM5091 */
	{0x94506d50, "3Com 3C509B-Combo EtherLink III (PnP)"},	/* TCM5094 */
	{0x95506d50, "3Com 3C509B-TPO EtherLink III (PnP)"},	/* TCM5095 */
	{0x98506d50, "3Com 3C509B-TPC EtherLink III (PnP)"},	/* TCM5098 */
	{0xf780d041, NULL},	/* PNP80f7 */
	{0, NULL},
};

/*
 * We get eeprom data from the id_port given an offset into the eeprom.
 * Basically; after the ID_sequence is sent to all of the cards; they enter
 * the ID_CMD state where they will accept command requests. 0x80-0xbf loads
 * the eeprom data.  We then read the port 16 times and with every read; the
 * cards check for contention (ie: if one card writes a 0 bit and another
 * writes a 1 bit then the host sees a 0. At the end of the cycle; each card
 * compares the data on the bus; if there is a difference then that card goes
 * into ID_WAIT state again). In the meantime; one bit of data is returned in
 * the AX register which is conveniently returned to us by inb().  Hence; we
 * read 16 times getting one bit of data with each read.
 */
#ifdef __i386__
static uint16_t
get_eeprom_data(int id_port, int offset)
{
	int i;
	uint16_t data = 0;

	outb(id_port, EEPROM_CMD_RD | offset);
	DELAY(BIT_DELAY_MULTIPLE * 1000);
	for (i = 0; i < 16; i++) {
		DELAY(50);
		data = (data << 1) | (inw(id_port) & 1);
	}
	return (data);
}
#endif

const char *
ep_isa_match_id(uint32_t id, struct isa_ident *isa_devs)
{
	struct isa_ident *i = isa_devs;

	while (i->name != NULL) {
		if (id == i->id)
			return (i->name);
		i++;
	}
	/*
	 * If we see a card that is likely to be a 3c509
	 * return something so that it will work; be annoying
	 * so that the user will tell us about it though.
	 */
	if ((id >> 4) == ISA_ID_3C509_XXX)
		return ("Unknown 3c509; notify maintainer!");
	return (NULL);
}

#ifdef __i386__
static void
ep_isa_identify(driver_t * driver, device_t parent)
{
	int tag = EP_LAST_TAG;
	int found = 0;
	int i;
	int j;
	const char *desc;
	uint16_t data;
	uint32_t irq;
	uint32_t ioport;
	uint32_t isa_id;
	device_t child;

	outb(ELINK_ID_PORT, 0);
	outb(ELINK_ID_PORT, 0);
	elink_idseq(ELINK_509_POLY);
	elink_reset();

	DELAY(DELAY_MULTIPLE * 10000);

	for (i = 0; i < EP_MAX_BOARDS; i++) {

		outb(ELINK_ID_PORT, 0);
		outb(ELINK_ID_PORT, 0);
		elink_idseq(ELINK_509_POLY);
		DELAY(400);

		/*
		 * For the first probe, clear all board's tag registers.
		 * Otherwise kill off already-found boards. -- linux 3c509.c
		 */
		if (i == 0)
			outb(ELINK_ID_PORT, 0xd0);
		else
			outb(ELINK_ID_PORT, 0xd8);

		/* Get out of loop if we're out of cards. */
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_MFG_ID);
		if (data != MFG_ID)
			break;
		/* resolve contention using the Ethernet address */
		for (j = 0; j < 3; j++)
			(void)get_eeprom_data(ELINK_ID_PORT, j);

		/*
		 * Construct an 'isa_id' in 'EISA' format.
		 */
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_MFG_ID);
		isa_id = (htons(data) << 16);
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_PROD_ID);
		isa_id |= htons(data);

		/* Find known ISA boards */
		desc = ep_isa_match_id(isa_id, ep_isa_devs);
		if (!desc) {
			if (bootverbose)
				device_printf(parent,
				    "unknown ID 0x%08x\n", isa_id);
			continue;
		}
		/* Retreive IRQ */
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_RESOURCE_CFG);
		irq = (data >> 12);

		/* Retreive IOPORT */
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_ADDR_CFG);
		ioport = (((data & ADDR_CFG_MASK) << 4) + 0x200);

		if ((data & ADDR_CFG_MASK) == ADDR_CFG_EISA) {
			device_printf(parent,
			    "<%s> at port 0x%03x in EISA mode, ignoring!\n",
			    desc, ioport);
			/*
			 * Set the adaptor tag so that the next card can be
			 * found.
			 */
			outb(ELINK_ID_PORT, tag--);
			continue;
		}
		/* Test for an adapter with PnP support. */
		data = get_eeprom_data(ELINK_ID_PORT, EEPROM_CAP);
		if (data == CAP_ISA) {
			data = get_eeprom_data(ELINK_ID_PORT,
			    EEPROM_INT_CONFIG_1);
			if (data & ICW1_IAS_PNP) {
				if (bootverbose)
					device_printf(parent,
					    "<%s> at 0x%03x "
					    "in PnP mode!\n",
					    desc, ioport);
				/*
				 * Set the adaptor tag so that the next card
				 * can be found.
				 */
				outb(ELINK_ID_PORT, tag--);
				continue;
			}
		}
		/* Set the adaptor tag so that the next card can be found. */
		outb(ELINK_ID_PORT, tag--);

		/* Activate the adaptor at the EEPROM location. */
		outb(ELINK_ID_PORT, ACTIVATE_ADAPTER_TO_CONFIG);

		/* Test for an adapter in TEST mode. */
		outw(ioport + EP_COMMAND, WINDOW_SELECT | 0);
		data = inw(ioport + EP_W0_EEPROM_COMMAND);
		if (data & EEPROM_TST_MODE) {
			device_printf(parent,
			    "<%s> at port 0x%03x in TEST mode!"
			    "  Erase pencil mark.\n",
			    desc, ioport);
			continue;
		}
		child = BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "ep", -1);
		device_set_desc_copy(child, desc);
		device_set_driver(child, driver);
		bus_set_resource(child, SYS_RES_IRQ, 0, irq, 1);
		bus_set_resource(child, SYS_RES_IOPORT, 0, ioport, EP_IOSIZE);

		if (bootverbose)
			device_printf(parent,
			    "<%s>"
			    " at port 0x%03x-0x%03x irq %d\n",
			    desc, ioport, ioport + EP_IOSIZE, irq);
		found++;
	}
}
#endif

static int
ep_isa_probe(device_t dev)
{
	int error = 0;

	/* Check isapnp ids */
	error = ISA_PNP_PROBE(device_get_parent(dev), dev, ep_ids);

	/* If the card had a PnP ID that didn't match any we know about */
	if (error == ENXIO)
		return (error);

	/* If we had some other problem. */
	if (!(error == 0 || error == ENOENT))
		return (error);

	/* If we have the resources we need then we're good to go. */
	if ((bus_get_resource_start(dev, SYS_RES_IOPORT, 0) != 0) &&
	    (bus_get_resource_start(dev, SYS_RES_IRQ, 0) != 0))
		return (0);

	return (ENXIO);
}

static int
ep_isa_attach(device_t dev)
{
	struct ep_softc *sc = device_get_softc(dev);
	int error = 0;

	if ((error = ep_alloc(dev)))
		goto bad;
	ep_get_media(sc);

	GO_WINDOW(sc, 0);
	SET_IRQ(sc, rman_get_start(sc->irq));

	if ((error = ep_attach(sc)))
		goto bad;
	error = ep_eeprom_cksum(sc);
	if (error) {
		device_printf(sc->dev, "Invalid EEPROM checksum!\n");
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

static int
ep_eeprom_cksum(struct ep_softc *sc)
{
	int i;
	int error;
	uint16_t val;
	uint16_t cksum;
	uint8_t cksum_high = 0;
	uint8_t cksum_low = 0;

	error = ep_get_e(sc, 0x0f, &val);
	if (error)
		return (ENXIO);
	cksum = val;

	for (i = 0; i < 0x0f; i++) {
		error = ep_get_e(sc, i, &val);
		if (error)
			return (ENXIO);
		switch (i) {
		case 0x08:
		case 0x09:
		case 0x0d:
			cksum_low ^= (uint8_t) (val & 0x00ff) ^
			    (uint8_t)((val & 0xff00) >> 8);
			break;
		default:
			cksum_high ^= (uint8_t) (val & 0x00ff) ^
			    (uint8_t)((val & 0xff00) >> 8);
			break;
		}
	}
	return (cksum != ((uint16_t)cksum_low | (uint16_t)(cksum_high << 8)));
}

static device_method_t ep_isa_methods[] = {
	/* Device interface */
#ifdef __i386__
	DEVMETHOD(device_identify, ep_isa_identify),
#endif
	DEVMETHOD(device_probe, ep_isa_probe),
	DEVMETHOD(device_attach, ep_isa_attach),
	DEVMETHOD(device_detach, ep_detach),

	DEVMETHOD_END
};

static driver_t ep_isa_driver = {
	"ep",
	ep_isa_methods,
	sizeof(struct ep_softc),
};

extern devclass_t ep_devclass;

DRIVER_MODULE(ep, isa, ep_isa_driver, ep_devclass, 0, 0);
#ifdef __i386__
MODULE_DEPEND(ep, elink, 1, 1, 1);
#endif
ISA_PNP_INFO(ep_ids);
