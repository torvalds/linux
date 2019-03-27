/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 M. Warner Losh <imp@village.org> 
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
 * Modifications for Megahertz X-Jack Ethernet Card (XJ-10BT)
 * 
 * Copyright (c) 1996 by Tatsumi Hosokawa <hosokawa@jp.FreeBSD.org>
 *                       BSD-nomads, Tokyo, Japan.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/ethernet.h> 
#include <net/if.h> 
#include <net/if_arp.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h> 

#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>
#include <dev/sn/if_snreg.h>
#include <dev/sn/if_snvar.h>

#include "card_if.h"
#include "pccarddevs.h"

typedef int sn_get_enaddr_t(device_t dev, u_char *eaddr);
typedef int sn_activate_t(device_t dev);

struct sn_sw
{
	int type;
#define SN_NORMAL 1
#define SN_MEGAHERTZ 2
#define SN_OSITECH 3
#define SN_OSI_SOD 4
#define SN_MOTO_MARINER 5
	char *typestr;
	sn_get_enaddr_t *get_mac;
	sn_activate_t *activate;
};

static sn_get_enaddr_t sn_pccard_normal_get_mac;
static sn_activate_t sn_pccard_normal_activate;
const static struct sn_sw sn_normal_sw = {
	SN_NORMAL, "plain",
	sn_pccard_normal_get_mac,
	sn_pccard_normal_activate
};

static sn_get_enaddr_t sn_pccard_megahertz_get_mac;
static sn_activate_t sn_pccard_megahertz_activate;
const static struct sn_sw sn_mhz_sw = {
	SN_MEGAHERTZ, "Megahertz",
	sn_pccard_megahertz_get_mac,
	sn_pccard_megahertz_activate
};

static const struct sn_product {
	struct pccard_product prod;
	const struct sn_sw *sw;
} sn_pccard_products[] = {
	{ PCMCIA_CARD(DSPSI, XJEM1144), &sn_mhz_sw },
	{ PCMCIA_CARD(DSPSI, XJACK), &sn_normal_sw },
/*	{ PCMCIA_CARD(MOTOROLA, MARINER), SN_MOTO_MARINER }, */
	{ PCMCIA_CARD(NEWMEDIA, BASICS), &sn_normal_sw },
	{ PCMCIA_CARD(MEGAHERTZ, VARIOUS), &sn_mhz_sw},
	{ PCMCIA_CARD(MEGAHERTZ, XJEM3336), &sn_mhz_sw},
/*	{ PCMCIA_CARD(OSITECH, TRUMP_SOD), SN_OSI_SOD }, */
/*	{ PCMCIA_CARD(OSITECH, TRUMP_JOH), SN_OSITECH }, */
/*	{ PCMCIA_CARD(PSION, GOLDCARD), SN_OSITECH }, */
/*	{ PCMCIA_CARD(PSION, NETGLOBAL), SNI_OSI_SOD }, */
/*	{ PCMCIA_CARD(PSION, NETGLOBAL2), SN_OSITECH }, */
	{ PCMCIA_CARD(SMC, 8020BT), &sn_normal_sw },
	{ PCMCIA_CARD(SMC, SMC91C96), &sn_normal_sw },
	{ { NULL } }
	
};

static const struct sn_product *
sn_pccard_lookup(device_t dev)
{

	return ((const struct sn_product *)
	    pccard_product_lookup(dev,
		(const struct pccard_product *)sn_pccard_products,
		sizeof(sn_pccard_products[0]), NULL));
}

static int
sn_pccard_probe(device_t dev)
{
	const struct sn_product *pp;

	if ((pp = sn_pccard_lookup(dev)) != NULL) {
		if (pp->prod.pp_name != NULL)
			device_set_desc(dev, pp->prod.pp_name);
		return 0;
	}
	return EIO;
}

static int
sn_pccard_ascii_enaddr(const char *str, u_char *enet)
{
        uint8_t digit;
	int i;

	memset(enet, 0, ETHER_ADDR_LEN);
	for (i = 0, digit = 0; i < (ETHER_ADDR_LEN * 2); i++) {
		if (str[i] >= '0' && str[i] <= '9')
			digit |= str[i] - '0';
		else if (str[i] >= 'a' && str[i] <= 'f')
			digit |= (str[i] - 'a') + 10;
		else if (str[i] >= 'A' && str[i] <= 'F')
			digit |= (str[i] - 'A') + 10;
		else
			return (0);		/* Bogus digit!! */

		/* Compensate for ordering of digits. */
		if (i & 1) {
			enet[i >> 1] = digit;
			digit = 0;
		} else
			digit <<= 4;
	}

	return (1);
}

static int
sn_pccard_normal_get_mac(device_t dev, u_char *eaddr)
{
	int i, sum;
	const char *cisstr;

	pccard_get_ether(dev, eaddr);
	for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
		sum |= eaddr[i];
	if (sum == 0) {
		pccard_get_cis3_str(dev, &cisstr);
		if (cisstr && strlen(cisstr) == ETHER_ADDR_LEN * 2)
		    sum = sn_pccard_ascii_enaddr(cisstr, eaddr);
	}
	if (sum == 0) {
		pccard_get_cis4_str(dev, &cisstr);
		if (cisstr && strlen(cisstr) == ETHER_ADDR_LEN * 2)
		    sum = sn_pccard_ascii_enaddr(cisstr, eaddr);
	}
	return sum;
}

static int
sn_pccard_normal_activate(device_t dev)
{
	int err;

	err = sn_activate(dev);
	if (err)
		sn_deactivate(dev);
	return (err);
}

static int
sn_pccard_megahertz_mac(const struct pccard_tuple *tuple, void *argp)
{
	uint8_t *enaddr = argp;
	int i;
	uint8_t buffer[ETHER_ADDR_LEN * 2];

	/* Code 0x81 is Megahertz' special cis node contianing the MAC */
	if (tuple->code != 0x81)
		return (0);

	/* Make sure this is a sane node, as ASCII digits */
	if (tuple->length != ETHER_ADDR_LEN * 2 + 1)
		return (0);

	/* Copy the MAC ADDR and return success if decoded */
	for (i = 0; i < ETHER_ADDR_LEN * 2; i++)
		buffer[i] = pccard_tuple_read_1(tuple, i);
	return (sn_pccard_ascii_enaddr(buffer, enaddr));
}

static int
sn_pccard_megahertz_get_mac(device_t dev, u_char *eaddr)
{

	if (sn_pccard_normal_get_mac(dev, eaddr))
		return 1;
	/*
	 * If that fails, try the special CIS tuple 0x81 that the
	 * '3288 and '3336 cards have.  That tuple specifies an ASCII
	 * string, ala CIS3 or CIS4 in the 'normal' cards.
	 */
	return (pccard_cis_scan(dev, sn_pccard_megahertz_mac, eaddr));
}

static int
sn_pccard_megahertz_activate(device_t dev)
{
	int err;
	struct sn_softc *sc = device_get_softc(dev);
	u_long start;

	err = sn_activate(dev);
	if (err) {
		sn_deactivate(dev);
		return (err);
	}
	/*
	 * CIS resource is the modem one, so save it away.
	 */
	sc->modem_rid = sc->port_rid;
	sc->modem_res = sc->port_res;

	/*
	 * The MHz XJEM/CCEM series of cards just need to have any
	 * old resource allocated for the ethernet side of things,
	 * provided bit 0x80 isn't set in the address.  That bit is
	 * evidentially reserved for modem function and is how the
	 * card steers the addresses internally.
	 */
	sc->port_res = NULL;
	start = 0;
	do
	{
		sc->port_rid = 1;
		sc->port_res = bus_alloc_resource(dev, SYS_RES_IOPORT,
		    &sc->port_rid, start, ~0, SMC_IO_EXTENT, RF_ACTIVE);
		if (sc->port_res == NULL)
			break;
		if (!(rman_get_start(sc->port_res) & 0x80))
			break;
		start = rman_get_start(sc->port_res) + SMC_IO_EXTENT;
		bus_release_resource(dev, SYS_RES_IOPORT, sc->port_rid,
		    sc->port_res);
	} while (start < 0xff80);
	if (sc->port_res == NULL) {
		sn_deactivate(dev);
		return ENOMEM;
	}
	return 0;
}

static int
sn_pccard_attach(device_t dev)
{
	struct sn_softc *sc = device_get_softc(dev);
	u_char eaddr[ETHER_ADDR_LEN];
	int i, err;
	uint16_t w;
	u_char sum;
	const struct sn_product *pp;

	pp = sn_pccard_lookup(dev);
	sum = pp->sw->get_mac(dev, eaddr);

	/* Allocate resources so we can program the ether addr */
	sc->dev = dev;
	err = pp->sw->activate(dev);
	if (err != 0)
		return (err);

	if (sum) {
		printf("Programming sn card's addr\n");
		SMC_SELECT_BANK(sc, 1);
		for (i = 0; i < 3; i++) {
			w = (uint16_t)eaddr[i * 2] | 
			    (((uint16_t)eaddr[i * 2 + 1]) << 8);
			CSR_WRITE_2(sc, IAR_ADDR0_REG_W + i * 2, w);
		}
	}
	err = sn_attach(dev);
	if (err)
		sn_deactivate(dev);
	return (err);
}

static device_method_t sn_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sn_pccard_probe),
	DEVMETHOD(device_attach,	sn_pccard_attach),
	DEVMETHOD(device_detach,	sn_detach),

	{ 0, 0 }
};

static driver_t sn_pccard_driver = {
	"sn",
	sn_pccard_methods,
	sizeof(struct sn_softc),
};

extern devclass_t sn_devclass;

DRIVER_MODULE(sn, pccard, sn_pccard_driver, sn_devclass, 0, 0);
MODULE_DEPEND(sn, ether, 1, 1, 1);
PCCARD_PNP_INFO(sn_pccard_products);
