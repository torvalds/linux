/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, M. Warner Losh
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/*
 * Notes for adding media support.  Each chipset is somewhat different
 * from the others.  Linux has a table of OIDs that it uses to see what
 * supports the misc register of the NS83903.  But a sampling of datasheets
 * I could dig up on cards I own paints a different picture.
 *
 * Chipset specific details:
 * NS 83903/902A paired
 *    ccr base 0x1020
 *    id register at 0x1000: 7-3 = 0, 2-0 = 1.
 *	(maybe this test is too week)
 *    misc register at 0x018:
 *	6 WAIT_TOUTENABLE enable watchdog timeout
 *	3 AUI/TPI 1 AUX, 0 TPI
 *	2 loopback
 *      1 gdlink (tpi mode only) 1 tp good, 0 tp bad
 *	0 0-no mam, 1 mam connected
 *
 * NS83926 appears to be a NS pcmcia glue chip used on the IBM Ethernet II
 * and the NEC PC9801N-J12 ccr base 0x2000!
 *
 * winbond 289c926
 *    ccr base 0xfd0
 *    cfb (am 0xff2):
 *	0-1 PHY01	00 TPI, 01 10B2, 10 10B5, 11 TPI (reduced squ)
 *	2 LNKEN		0 - enable link and auto switch, 1 disable
 *	3 LNKSTS	TPI + LNKEN=0 + link good == 1, else 0
 *    sr (am 0xff4)
 *	88 00 88 00 88 00, etc
 *
 * TMI tc3299a (cr PHY01 == 0)
 *    ccr base 0x3f8
 *    cra (io 0xa)
 *    crb (io 0xb)
 *	0-1 PHY01	00 auto, 01 res, 10 10B5, 11 TPI
 *	2 GDLINK	1 disable checking of link
 *	6 LINK		0 bad link, 1 good link
 *
 * EN5017A, EN5020	no data, but very popular
 * Other chips?
 * NetBSD supports RTL8019, but none have surfaced that I can see
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>
#include <dev/ed/ax88x90reg.h>
#include <dev/ed/dl100xxreg.h>
#include <dev/ed/tc5299jreg.h>
#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccard_cis.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "card_if.h"
/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"
#include "pccarddevs.h"

/*
 * NE-2000 based PC Cards have a number of ways to get the MAC address.
 * Some cards encode this as a FUNCE.  Others have this in the ROMs the
 * same way that ISA cards do.  Some have it encoded in the attribute
 * memory somewhere that isn't in the CIS.  Some new chipsets have it
 * in special registers in the ASIC part of the chip.
 *
 * For those cards that have the MAC adress stored in attribute memory
 * outside of a FUNCE entry in the CIS, nearly all of them have it at
 * a fixed offset (0xff0).  We use that offset as a source of last
 * resource if other offsets have failed.  This is the address of the
 * National Semiconductor DP83903A, which is the only chip's datasheet
 * I've found.
 */
#define ED_DEFAULT_MAC_OFFSET	0xff0

static const struct ed_product {
	struct pccard_product	prod;
	int flags;
#define	NE2000DVF_DL100XX	0x0001		/* chip is D-Link DL10019/22 */
#define	NE2000DVF_AX88X90	0x0002		/* chip is ASIX AX88[17]90 */
#define NE2000DVF_TC5299J	0x0004		/* chip is Tamarack TC5299J */
#define NE2000DVF_TOSHIBA	0x0008		/* Toshiba DP83902A */
#define NE2000DVF_ENADDR	0x0100		/* Get MAC from attr mem */
#define NE2000DVF_ANYFUNC	0x0200		/* Allow any function type */
#define NE2000DVF_MODEM		0x0400		/* Has a modem/serial */
	int enoff;
} ed_pccard_products[] = {
	{ PCMCIA_CARD(ACCTON, EN2212), 0},
	{ PCMCIA_CARD(ACCTON, EN2216), 0},
	{ PCMCIA_CARD(ALLIEDTELESIS, LA_PCM), 0},
	{ PCMCIA_CARD(AMBICOM, AMB8002), 0},
	{ PCMCIA_CARD(AMBICOM, AMB8002T), 0},
	{ PCMCIA_CARD(AMBICOM, AMB8010), 0},
	{ PCMCIA_CARD(AMBICOM, AMB8010_ALT), 0},
	{ PCMCIA_CARD(AMBICOM, AMB8610), 0},
	{ PCMCIA_CARD(BILLIONTON, CFLT10N), 0},
	{ PCMCIA_CARD(BILLIONTON, LNA100B), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(BILLIONTON, LNT10TB), 0},
	{ PCMCIA_CARD(BILLIONTON, LNT10TN), 0},
	{ PCMCIA_CARD(BROMAX, AXNET), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(BROMAX, IPORT), 0},
	{ PCMCIA_CARD(BROMAX, IPORT2), 0},
	{ PCMCIA_CARD(BUFFALO, LPC2_CLT), 0},
	{ PCMCIA_CARD(BUFFALO, LPC3_CLT), 0},
	{ PCMCIA_CARD(BUFFALO, LPC3_CLX), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(BUFFALO, LPC4_TX), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(BUFFALO, LPC4_CLX), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(BUFFALO, LPC_CF_CLT), 0},
	{ PCMCIA_CARD(CNET, NE2000), 0},
	{ PCMCIA_CARD(COMPEX, AX88190), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(COMPEX, LANMODEM), 0},
	{ PCMCIA_CARD(COMPEX, LINKPORT_ENET_B), 0},
	{ PCMCIA_CARD(COREGA, ETHER_II_PCC_T), 0},
	{ PCMCIA_CARD(COREGA, ETHER_II_PCC_TD), 0},
	{ PCMCIA_CARD(COREGA, ETHER_PCC_T), 0},
	{ PCMCIA_CARD(COREGA, ETHER_PCC_TD), 0},
	{ PCMCIA_CARD(COREGA, FAST_ETHER_PCC_TX), NE2000DVF_DL100XX},
	{ PCMCIA_CARD(COREGA, FETHER_PCC_TXD), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(COREGA, FETHER_PCC_TXF), NE2000DVF_DL100XX},
	{ PCMCIA_CARD(COREGA, FETHER_II_PCC_TXD), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(COREGA, LAPCCTXD), 0},
	{ PCMCIA_CARD(DAYNA, COMMUNICARD_E_1), 0},
	{ PCMCIA_CARD(DAYNA, COMMUNICARD_E_2), 0},
	{ PCMCIA_CARD(DLINK, DE650), NE2000DVF_ANYFUNC },
	{ PCMCIA_CARD(DLINK, DE660), 0 },
	{ PCMCIA_CARD(DLINK, DE660PLUS), 0},
	{ PCMCIA_CARD(DYNALINK, L10C), 0},
	{ PCMCIA_CARD(EDIMAX, EP4000A), 0},
	{ PCMCIA_CARD(EPSON, EEN10B), 0},
	{ PCMCIA_CARD(EXP, THINLANCOMBO), 0},
	{ PCMCIA_CARD(GLOBALVILLAGE, LANMODEM), 0},
	{ PCMCIA_CARD(GREY_CELL, TDK3000), 0},
	{ PCMCIA_CARD(GREY_CELL, DMF650TX),
	    NE2000DVF_ANYFUNC | NE2000DVF_DL100XX | NE2000DVF_MODEM},
	{ PCMCIA_CARD(GVC, NIC_2000P), 0},
	{ PCMCIA_CARD(IBM, HOME_AND_AWAY), 0},
	{ PCMCIA_CARD(IBM, INFOMOVER), 0},
	{ PCMCIA_CARD(IODATA3, PCLAT), 0},
	{ PCMCIA_CARD(KINGSTON, CIO10T), 0},
	{ PCMCIA_CARD(KINGSTON, KNE2), 0},
	{ PCMCIA_CARD(LANTECH, FASTNETTX), NE2000DVF_AX88X90},
	/* Same ID for many different cards, including generic NE2000 */
	{ PCMCIA_CARD(LINKSYS, COMBO_ECARD),
	    NE2000DVF_DL100XX | NE2000DVF_AX88X90},
	{ PCMCIA_CARD(LINKSYS, ECARD_1), 0},
	{ PCMCIA_CARD(LINKSYS, ECARD_2), 0},
	{ PCMCIA_CARD(LINKSYS, ETHERFAST), NE2000DVF_DL100XX},
	{ PCMCIA_CARD(LINKSYS, TRUST_COMBO_ECARD), 0},
	{ PCMCIA_CARD(MACNICA, ME1_JEIDA), 0},
	{ PCMCIA_CARD(MAGICRAM, ETHER), 0},
	{ PCMCIA_CARD(MELCO, LPC3_CLX), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(MELCO, LPC3_TX), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(MELCO2, LPC2_T), 0},
	{ PCMCIA_CARD(MELCO2, LPC2_TX), 0},
	{ PCMCIA_CARD(MITSUBISHI, B8895), NE2000DVF_ANYFUNC}, /* NG */
	{ PCMCIA_CARD(MICRORESEARCH, MR10TPC), 0},
	{ PCMCIA_CARD(NDC, ND5100_E), 0},
	{ PCMCIA_CARD(NETGEAR, FA410TXC), NE2000DVF_DL100XX},
	/* Same ID as DLINK DFE-670TXD.  670 has DL10022, fa411 has ax88790 */
	{ PCMCIA_CARD(NETGEAR, FA411), NE2000DVF_AX88X90 | NE2000DVF_DL100XX},
	{ PCMCIA_CARD(NEXTCOM, NEXTHAWK), 0},
	{ PCMCIA_CARD(NEWMEDIA, LANSURFER), NE2000DVF_ANYFUNC},
	{ PCMCIA_CARD(NEWMEDIA, LIVEWIRE), 0},
	{ PCMCIA_CARD(OEM2, 100BASE), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(OEM2, ETHERNET), 0},
	{ PCMCIA_CARD(OEM2, FAST_ETHERNET), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(OEM2, NE2000), 0},
	{ PCMCIA_CARD(PLANET, SMARTCOM2000), 0 },
	{ PCMCIA_CARD(PREMAX, PE200), 0},
	{ PCMCIA_CARD(PSION, LANGLOBAL),
	    NE2000DVF_ANYFUNC | NE2000DVF_AX88X90 | NE2000DVF_MODEM},
	{ PCMCIA_CARD(RACORE, ETHERNET), 0},
	{ PCMCIA_CARD(RACORE, FASTENET), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(RACORE, 8041TX), NE2000DVF_AX88X90 | NE2000DVF_TC5299J},
	{ PCMCIA_CARD(RELIA, COMBO), 0},
	{ PCMCIA_CARD(RIOS, PCCARD3), 0},
	{ PCMCIA_CARD(RPTI, EP400), 0},
	{ PCMCIA_CARD(RPTI, EP401), 0},
	{ PCMCIA_CARD(SMC, EZCARD), 0},
	{ PCMCIA_CARD(SOCKET, EA_ETHER), 0},
	{ PCMCIA_CARD(SOCKET, ES_1000), 0},
	{ PCMCIA_CARD(SOCKET, LP_ETHER), 0},
	{ PCMCIA_CARD(SOCKET, LP_ETHER_CF), 0},
	{ PCMCIA_CARD(SOCKET, LP_ETH_10_100_CF), NE2000DVF_DL100XX},
	{ PCMCIA_CARD(SVEC, COMBOCARD), 0},
	{ PCMCIA_CARD(SVEC, LANCARD), 0},
	{ PCMCIA_CARD(TAMARACK, ETHERNET), 0},
	{ PCMCIA_CARD(TDK, CFE_10), 0},
	{ PCMCIA_CARD(TDK, LAK_CD031), 0},
	{ PCMCIA_CARD(TDK, DFL5610WS), 0},
	{ PCMCIA_CARD(TELECOMDEVICE, LM5LT), 0 },
	{ PCMCIA_CARD(TELECOMDEVICE, TCD_HPC100), NE2000DVF_AX88X90},
	{ PCMCIA_CARD(TJ, PTJ_LAN_T), 0 },
	{ PCMCIA_CARD(TOSHIBA2, LANCT00A), NE2000DVF_ANYFUNC | NE2000DVF_TOSHIBA},
	{ PCMCIA_CARD(ZONET, ZEN), 0},
	{ { NULL } }
};

/*
 * MII bit-bang glue
 */
static uint32_t ed_pccard_dl100xx_mii_bitbang_read(device_t dev);
static void ed_pccard_dl100xx_mii_bitbang_write(device_t dev, uint32_t val);

static const struct mii_bitbang_ops ed_pccard_dl100xx_mii_bitbang_ops = {
	ed_pccard_dl100xx_mii_bitbang_read,
	ed_pccard_dl100xx_mii_bitbang_write,
	{
		ED_DL100XX_MII_DATAOUT,	/* MII_BIT_MDO */
		ED_DL100XX_MII_DATAIN,	/* MII_BIT_MDI */
		ED_DL100XX_MII_CLK,	/* MII_BIT_MDC */
		ED_DL100XX_MII_DIROUT,	/* MII_BIT_DIR_HOST_PHY */
		0			/* MII_BIT_DIR_PHY_HOST */
	}
};

static uint32_t ed_pccard_ax88x90_mii_bitbang_read(device_t dev);
static void ed_pccard_ax88x90_mii_bitbang_write(device_t dev, uint32_t val);

static const struct mii_bitbang_ops ed_pccard_ax88x90_mii_bitbang_ops = {
	ed_pccard_ax88x90_mii_bitbang_read,
	ed_pccard_ax88x90_mii_bitbang_write,
	{
		ED_AX88X90_MII_DATAOUT,	/* MII_BIT_MDO */
		ED_AX88X90_MII_DATAIN,	/* MII_BIT_MDI */
		ED_AX88X90_MII_CLK,	/* MII_BIT_MDC */
		0,			/* MII_BIT_DIR_HOST_PHY */
		ED_AX88X90_MII_DIRIN	/* MII_BIT_DIR_PHY_HOST */
	}
};

static uint32_t ed_pccard_tc5299j_mii_bitbang_read(device_t dev);
static void ed_pccard_tc5299j_mii_bitbang_write(device_t dev, uint32_t val);

static const struct mii_bitbang_ops ed_pccard_tc5299j_mii_bitbang_ops = {
	ed_pccard_tc5299j_mii_bitbang_read,
	ed_pccard_tc5299j_mii_bitbang_write,
	{
		ED_TC5299J_MII_DATAOUT,	/* MII_BIT_MDO */
		ED_TC5299J_MII_DATAIN,	/* MII_BIT_MDI */
		ED_TC5299J_MII_CLK,	/* MII_BIT_MDC */
		0,			/* MII_BIT_DIR_HOST_PHY */
		ED_AX88X90_MII_DIRIN	/* MII_BIT_DIR_PHY_HOST */
	}
};

/*
 *      PC Card (PCMCIA) specific code.
 */
static int	ed_pccard_probe(device_t);
static int	ed_pccard_attach(device_t);
static void	ed_pccard_tick(struct ed_softc *);

static int	ed_pccard_dl100xx(device_t dev, const struct ed_product *);
static void	ed_pccard_dl100xx_mii_reset(struct ed_softc *sc);

static int	ed_pccard_ax88x90(device_t dev, const struct ed_product *);

static int	ed_miibus_readreg(device_t dev, int phy, int reg);
static int	ed_ifmedia_upd(struct ifnet *);
static void	ed_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int	ed_pccard_tc5299j(device_t dev, const struct ed_product *);

static void
ed_pccard_print_entry(const struct ed_product *pp)
{
	int i;

	printf("Product entry: ");
	if (pp->prod.pp_name)
		printf("name='%s',", pp->prod.pp_name);
	printf("vendor=%#x,product=%#x", pp->prod.pp_vendor,
	    pp->prod.pp_product);
	for (i = 0; i < 4; i++)
		if (pp->prod.pp_cis[i])
			printf(",CIS%d='%s'", i, pp->prod.pp_cis[i]);
	printf("\n");
}

static int
ed_pccard_probe(device_t dev)
{
	const struct ed_product *pp, *pp2;
	int		error, first = 1;
	uint32_t	fcn = PCCARD_FUNCTION_UNSPEC;

	/* Make sure we're a network function */
	error = pccard_get_function(dev, &fcn);
	if (error != 0)
		return (error);

	if ((pp = (const struct ed_product *) pccard_product_lookup(dev, 
	    (const struct pccard_product *) ed_pccard_products,
	    sizeof(ed_pccard_products[0]), NULL)) != NULL) {
		if (pp->prod.pp_name != NULL)
			device_set_desc(dev, pp->prod.pp_name);
		/*
		 * Some devices don't ID themselves as network, but
		 * that's OK if the flags say so.
		 */
		if (!(pp->flags & NE2000DVF_ANYFUNC) &&
		    fcn != PCCARD_FUNCTION_NETWORK)
			return (ENXIO);
		/*
		 * Some devices match multiple entries.  Report that
		 * as a warning to help cull the table
		 */
		pp2 = pp;
		while ((pp2 = (const struct ed_product *)pccard_product_lookup(
		    dev, (const struct pccard_product *)(pp2 + 1),
		    sizeof(ed_pccard_products[0]), NULL)) != NULL) {
			if (first) {
				device_printf(dev,
    "Warning: card matches multiple entries.  Report to imp@freebsd.org\n");
				ed_pccard_print_entry(pp);
				first = 0;
			}
			ed_pccard_print_entry(pp2);
		}
		
		return (0);
	}
	return (ENXIO);
}

static int
ed_pccard_rom_mac(device_t dev, uint8_t *enaddr)
{
	struct ed_softc *sc = device_get_softc(dev);
	uint8_t romdata[32], sum;
	int i;

	/*
	 * Read in the rom data at location 0.  Since there are no
	 * NE-1000 based PC Card devices, we'll assume we're 16-bit.
	 *
	 * In researching what format this takes, I've found that the
	 * following appears to be true for multiple cards based on
	 * observation as well as datasheet digging.
	 *
	 * Data is stored in some ROM and is copied out 8 bits at a time
	 * into 16-bit wide locations.  This means that the odd locations
	 * of the ROM are not used (and can be either 0 or ff).
	 *
	 * The contents appears to be as follows:
	 * PROM   RAM
	 * Offset Offset	What
	 *  0      0	ENETADDR 0
	 *  1      2	ENETADDR 1
	 *  2      4	ENETADDR 2
	 *  3      6	ENETADDR 3
	 *  4      8	ENETADDR 4
	 *  5     10	ENETADDR 5
	 *  6-13  12-26 Reserved (varies by manufacturer)
	 * 14     28	0x57
	 * 15     30    0x57
	 *
	 * Some manufacturers have another image of enetaddr from
	 * PROM offset 0x10 to 0x15 with 0x42 in 0x1e and 0x1f, but
	 * this doesn't appear to be universally documented in the
	 * datasheets.  Some manufactuers have a card type, card config
	 * checksums, etc encoded into PROM offset 6-13, but deciphering it
	 * requires more knowledge about the exact underlying chipset than
	 * we possess (and maybe can possess).
	 */
	ed_pio_readmem(sc, 0, romdata, 32);
	if (bootverbose)
		device_printf(dev, "ROM DATA: %32D\n", romdata, " ");
	if (romdata[28] != 0x57 || romdata[30] != 0x57)
		return (0);
	for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
		sum |= romdata[i * 2];
	if (sum == 0)
		return (0);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		enaddr[i] = romdata[i * 2];
	return (1);
}

static int
ed_pccard_add_modem(device_t dev)
{
	device_printf(dev, "Need to write this code\n");
	return 0;
}

static int
ed_pccard_kick_phy(struct ed_softc *sc)
{
	struct mii_softc *miisc;
	struct mii_data *mii;

	mii = device_get_softc(sc->miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	return (mii_mediachg(mii));
}

static int
ed_pccard_media_ioctl(struct ed_softc *sc, struct ifreq *ifr, u_long command)
{
	struct mii_data *mii;

	if (sc->miibus == NULL)
		return (EINVAL);
	mii = device_get_softc(sc->miibus);
	return (ifmedia_ioctl(sc->ifp, ifr, &mii->mii_media, command));
}


static void
ed_pccard_mediachg(struct ed_softc *sc)
{
	struct mii_data *mii;

	if (sc->miibus == NULL)
		return;
	mii = device_get_softc(sc->miibus);
	mii_mediachg(mii);
}

static int
ed_pccard_attach(device_t dev)
{
	u_char sum;
	u_char enaddr[ETHER_ADDR_LEN];
	const struct ed_product *pp;
	int	error, i, flags, port_rid, modem_rid;
	struct ed_softc *sc = device_get_softc(dev);
	u_long size;
	static uint16_t *intr_vals[] = {NULL, NULL};

	sc->dev = dev;
	if ((pp = (const struct ed_product *) pccard_product_lookup(dev, 
	    (const struct pccard_product *) ed_pccard_products,
		 sizeof(ed_pccard_products[0]), NULL)) == NULL) {
		printf("Can't find\n");
		return (ENXIO);
	}
	modem_rid = port_rid = -1;
	if (pp->flags & NE2000DVF_MODEM) {
		for (i = 0; i < 4; i++) {
			size = bus_get_resource_count(dev, SYS_RES_IOPORT, i);
			if (size == ED_NOVELL_IO_PORTS)
				port_rid = i;
			else if (size == 8)
				modem_rid = i;
		}
		if (port_rid == -1) {
			device_printf(dev, "Cannot locate my ports!\n");
			return (ENXIO);
		}
	} else {
		port_rid = 0;
	}
	/* Allocate the port resource during setup. */
	error = ed_alloc_port(dev, port_rid, ED_NOVELL_IO_PORTS);
	if (error) {
		printf("alloc_port failed\n");
		return (error);
	}
	if (rman_get_size(sc->port_res) == ED_NOVELL_IO_PORTS / 2) {
		port_rid++;
		sc->port_res2 = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		    &port_rid, RF_ACTIVE);
		if (sc->port_res2 == NULL ||
		    rman_get_size(sc->port_res2) != ED_NOVELL_IO_PORTS / 2) {
			error = ENXIO;
			goto bad;
		}
	}
	error = ed_alloc_irq(dev, 0, 0);
	if (error)
		goto bad;

	/*
	 * Determine which chipset we are.  Almost all the PC Card chipsets
	 * have the Novel ASIC and NIC offsets.  There's 2 known cards that
	 * follow the WD80x3 conventions, which are handled as a special case.
	 */
	sc->asic_offset = ED_NOVELL_ASIC_OFFSET;
	sc->nic_offset  = ED_NOVELL_NIC_OFFSET;
	error = ENXIO;
	flags = device_get_flags(dev);
	if (error != 0)
		error = ed_pccard_dl100xx(dev, pp);
	if (error != 0)
		error = ed_pccard_ax88x90(dev, pp);
	if (error != 0)
		error = ed_pccard_tc5299j(dev, pp);
	if (error != 0) {
		error = ed_probe_Novell_generic(dev, flags);
		printf("Novell generic probe failed: %d\n", error);
	}
	if (error != 0 && (pp->flags & NE2000DVF_TOSHIBA)) {
		flags |= ED_FLAGS_TOSH_ETHER;
		flags |= ED_FLAGS_PCCARD;
		sc->asic_offset = ED_WD_ASIC_OFFSET;
		sc->nic_offset  = ED_WD_NIC_OFFSET;
		error = ed_probe_WD80x3_generic(dev, flags, intr_vals);
	}
	if (error)
		goto bad;

	/*
	 * There are several ways to get the MAC address for the card.
	 * Some of the above probe routines can fill in the enaddr.  If
	 * not, we run through a number of 'well known' locations:
	 *	(1) From the PC Card FUNCE
	 *	(2) From offset 0 in the shared memory
	 *	(3) From a hinted offset in attribute memory
	 *	(4) From 0xff0 in attribute memory
	 * If we can't get a non-zero MAC address from this list, we fail.
	 */
	for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
		sum |= sc->enaddr[i];
	if (sum == 0) {
		pccard_get_ether(dev, enaddr);
		if (bootverbose)
			device_printf(dev, "CIS MAC %6D\n", enaddr, ":");
		for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
			sum |= enaddr[i];
		if (sum == 0 && ed_pccard_rom_mac(dev, enaddr)) {
			if (bootverbose)
				device_printf(dev, "ROM mac %6D\n", enaddr,
				    ":");
			sum++;
		}
		if (sum == 0 && pp->flags & NE2000DVF_ENADDR) {
			for (i = 0; i < ETHER_ADDR_LEN; i++) {
				pccard_attr_read_1(dev, pp->enoff + i * 2,
				    enaddr + i);
				sum |= enaddr[i];
			}
			if (bootverbose)
				device_printf(dev, "Hint %x MAC %6D\n",
				    pp->enoff, enaddr, ":");
		}
		if (sum == 0) {
			for (i = 0; i < ETHER_ADDR_LEN; i++) {
				pccard_attr_read_1(dev, ED_DEFAULT_MAC_OFFSET +
				    i * 2, enaddr + i);
				sum |= enaddr[i];
			}
			if (bootverbose)
				device_printf(dev, "Fallback MAC %6D\n",
				    enaddr, ":");
		}
		if (sum == 0) {
			device_printf(dev, "Cannot extract MAC address.\n");
			ed_release_resources(dev);
			return (ENXIO);
		}
		bcopy(enaddr, sc->enaddr, ETHER_ADDR_LEN);
	}

	error = ed_attach(dev);
	if (error)
		goto bad;
 	if (sc->chip_type == ED_CHIP_TYPE_DL10019 ||
	    sc->chip_type == ED_CHIP_TYPE_DL10022) {
		/* Try to attach an MII bus, but ignore errors. */
		ed_pccard_dl100xx_mii_reset(sc);
		(void)mii_attach(dev, &sc->miibus, sc->ifp, ed_ifmedia_upd,
		    ed_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY,
		    MII_OFFSET_ANY, MIIF_FORCEANEG);
	} else if (sc->chip_type == ED_CHIP_TYPE_AX88190 ||
	    sc->chip_type == ED_CHIP_TYPE_AX88790 ||
	    sc->chip_type == ED_CHIP_TYPE_TC5299J) {
		error = mii_attach(dev, &sc->miibus, sc->ifp, ed_ifmedia_upd,
		    ed_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY,
		    MII_OFFSET_ANY, MIIF_FORCEANEG);
		if (error != 0) {
			device_printf(dev, "attaching PHYs failed\n");
			goto bad;
		}
	}
	if (sc->miibus != NULL) {
		sc->sc_tick = ed_pccard_tick;
		sc->sc_mediachg = ed_pccard_mediachg;
		sc->sc_media_ioctl = ed_pccard_media_ioctl;
		ed_pccard_kick_phy(sc);
	} else {
		ed_gen_ifmedia_init(sc);
	}
	if (modem_rid != -1)
		ed_pccard_add_modem(dev);

	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, edintr, sc, &sc->irq_handle);
	if (error) {
		device_printf(dev, "setup intr failed %d \n", error);
		goto bad;
	}	      

	return (0);
bad:
	ed_detach(dev);
	return (error);
}

/*
 * Probe the Ethernet MAC addrees for PCMCIA Linksys EtherFast 10/100 
 * and compatible cards (DL10019C Ethernet controller).
 */
static int
ed_pccard_dl100xx(device_t dev, const struct ed_product *pp)
{
	struct ed_softc *sc = device_get_softc(dev);
	u_char sum;
	uint8_t id;
	u_int   memsize;
	int i, error;

	if (!(pp->flags & NE2000DVF_DL100XX))
		return (ENXIO);
	if (bootverbose)
		device_printf(dev, "Trying DL100xx\n");
	error = ed_probe_Novell_generic(dev, device_get_flags(dev));
	if (bootverbose && error)
		device_printf(dev, "Novell generic probe failed: %d\n", error);
	if (error != 0)
		return (error);

	/*
	 * Linksys registers(offset from ASIC base)
	 *
	 * 0x04-0x09 : Physical Address Register 0-5 (PAR0-PAR5)
	 * 0x0A      : Card ID Register (CIR)
	 * 0x0B      : Check Sum Register (SR)
	 */
	for (sum = 0, i = 0x04; i < 0x0c; i++)
		sum += ed_asic_inb(sc, i);
	if (sum != 0xff) {
		if (bootverbose)
			device_printf(dev, "Bad checksum %#x\n", sum);
		return (ENXIO);		/* invalid DL10019C */
	}
	if (bootverbose)
		device_printf(dev, "CIR is %d\n", ed_asic_inb(sc, 0xa));
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc->enaddr[i] = ed_asic_inb(sc, 0x04 + i);
	ed_nic_outb(sc, ED_P0_DCR, ED_DCR_WTS | ED_DCR_FT1 | ED_DCR_LS);
	id = ed_asic_inb(sc, 0xf);
	sc->isa16bit = 1;
	/*
	 * Hard code values based on the datasheet.  We're NE-2000 compatible
	 * NIC with 24kb of packet memory starting at 24k offset.  These
	 * cards also work with 16k at 16k, but don't work with 24k at 16k
	 * or 32k at 16k.
	 */
	sc->type = ED_TYPE_NE2000;
	sc->mem_start = 24 * 1024;
	memsize = sc->mem_size = 24 * 1024;
	sc->mem_end = sc->mem_start + memsize;
	sc->tx_page_start = memsize / ED_PAGE_SIZE;
	sc->txb_cnt = 3;
	sc->rec_page_start = sc->tx_page_start + sc->txb_cnt * ED_TXBUF_SIZE;
	sc->rec_page_stop = sc->tx_page_start + memsize / ED_PAGE_SIZE;

	sc->mem_ring = sc->mem_start + sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE;

	ed_nic_outb(sc, ED_P0_PSTART, sc->mem_start / ED_PAGE_SIZE);
	ed_nic_outb(sc, ED_P0_PSTOP, sc->mem_end / ED_PAGE_SIZE);
	sc->vendor = ED_VENDOR_NOVELL;
	sc->chip_type = (id & 0x90) == 0x90 ?
	    ED_CHIP_TYPE_DL10022 : ED_CHIP_TYPE_DL10019;
	sc->type_str = ((id & 0x90) == 0x90) ? "DL10022" : "DL10019";
	sc->mii_bitbang_ops = &ed_pccard_dl100xx_mii_bitbang_ops;
	return (0);
}

/* MII bit-twiddling routines for cards using Dlink chipset */

static void
ed_pccard_dl100xx_mii_reset(struct ed_softc *sc)
{
	if (sc->chip_type != ED_CHIP_TYPE_DL10022)
		return;

	ed_asic_outb(sc, ED_DL100XX_MIIBUS, ED_DL10022_MII_RESET2);
	DELAY(10);
	ed_asic_outb(sc, ED_DL100XX_MIIBUS,
	    ED_DL10022_MII_RESET2 | ED_DL10022_MII_RESET1);
	DELAY(10);
	ed_asic_outb(sc, ED_DL100XX_MIIBUS, ED_DL10022_MII_RESET2);
	DELAY(10);
	ed_asic_outb(sc, ED_DL100XX_MIIBUS,
	    ED_DL10022_MII_RESET2 | ED_DL10022_MII_RESET1);
	DELAY(10);
	ed_asic_outb(sc, ED_DL100XX_MIIBUS, 0);
}

static void
ed_pccard_dl100xx_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct ed_softc *sc;

	sc = device_get_softc(dev);

	ed_asic_outb(sc, ED_DL100XX_MIIBUS, val);
	ed_asic_barrier(sc, ED_DL100XX_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static uint32_t
ed_pccard_dl100xx_mii_bitbang_read(device_t dev)
{
	struct ed_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = ed_asic_inb(sc, ED_DL100XX_MIIBUS);
	ed_asic_barrier(sc, ED_DL100XX_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (val);
}

static void
ed_pccard_ax88x90_reset(struct ed_softc *sc)
{
	int i;

	/* Reset Card */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_STP | ED_CR_PAGE_0);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_asic_outb(sc, ED_NOVELL_RESET, ed_asic_inb(sc, ED_NOVELL_RESET));

	/* Wait for the RST bit to assert, but cap it at 10ms */
	for (i = 10000; !(ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RST) && i > 0;
	     i--)
		continue;
	ed_nic_outb(sc, ED_P0_ISR, ED_ISR_RST);	/* ACK INTR */
	if (i == 0)
		device_printf(sc->dev, "Reset didn't finish\n");
}

/*
 * Probe and vendor-specific initialization routine for ax88x90 boards
 */
static int
ed_probe_ax88x90_generic(device_t dev, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	u_int   memsize;
	static char test_pattern[32] = "THIS is A memory TEST pattern";
	char    test_buffer[32];

	ed_pccard_ax88x90_reset(sc);
	DELAY(10*1000);

	/* Make sure that we really have an 8390 based board */
	if (!ed_probe_generic8390(sc))
		return (ENXIO);

	sc->vendor = ED_VENDOR_NOVELL;
	sc->mem_shared = 0;
	sc->cr_proto = ED_CR_RD2;

	/*
	 * This prevents packets from being stored in the NIC memory when the
	 * readmem routine turns on the start bit in the CR.  We write some
	 * bytes in word mode and verify we can read them back.  If we can't
	 * then we don't have an AX88x90 chip here.
	 */
	sc->isa16bit = 1;
	ed_nic_outb(sc, ED_P0_RCR, ED_RCR_MON);
	ed_nic_outb(sc, ED_P0_DCR, ED_DCR_WTS | ED_DCR_FT1 | ED_DCR_LS);
	ed_pio_writemem(sc, test_pattern, 16384, sizeof(test_pattern));
	ed_pio_readmem(sc, 16384, test_buffer, sizeof(test_pattern));
	if (bcmp(test_pattern, test_buffer, sizeof(test_pattern)) != 0)
		return (ENXIO);

	/*
	 * Hard code values based on the datasheet.  We're NE-2000 compatible
	 * NIC with 16kb of packet memory starting at 16k offset.
	 */
	sc->type = ED_TYPE_NE2000;
	memsize = sc->mem_size = 16*1024;
	sc->mem_start = 16 * 1024;
	if (ed_asic_inb(sc, ED_AX88X90_TEST) != 0)
		sc->chip_type = ED_CHIP_TYPE_AX88790;
	else {
		sc->chip_type = ED_CHIP_TYPE_AX88190;
		/*
		 * The AX88190 (not A) has external 64k SRAM.  Probe for this
		 * here.  Most of the cards I have either use the AX88190A
		 * part, or have only 32k SRAM for some reason, so I don't
		 * know if this works or not.
		 */
		ed_pio_writemem(sc, test_pattern, 32768, sizeof(test_pattern));
		ed_pio_readmem(sc, 32768, test_buffer, sizeof(test_pattern));
		if (bcmp(test_pattern, test_buffer, sizeof(test_pattern)) == 0) {
			sc->mem_start = 2*1024;
			memsize = sc->mem_size = 62 * 1024;
		}
	}
	sc->mem_end = sc->mem_start + memsize;
	sc->tx_page_start = memsize / ED_PAGE_SIZE;
	if (sc->mem_size > 16 * 1024)
		sc->txb_cnt = 3;
	else
		sc->txb_cnt = 2;
	sc->rec_page_start = sc->tx_page_start + sc->txb_cnt * ED_TXBUF_SIZE;
	sc->rec_page_stop = sc->tx_page_start + memsize / ED_PAGE_SIZE;

	sc->mem_ring = sc->mem_start + sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE;

	ed_nic_outb(sc, ED_P0_PSTART, sc->mem_start / ED_PAGE_SIZE);
	ed_nic_outb(sc, ED_P0_PSTOP, sc->mem_end / ED_PAGE_SIZE);

	/* Get the mac before we go -- It's just at 0x400 in "SRAM" */
	ed_pio_readmem(sc, 0x400, sc->enaddr, ETHER_ADDR_LEN);

	/* clear any pending interrupts that might have occurred above */
	ed_nic_outb(sc, ED_P0_ISR, 0xff);
	sc->sc_write_mbufs = ed_pio_write_mbufs;
	return (0);
}

static int
ed_pccard_ax88x90_check_mii(device_t dev, struct ed_softc *sc)
{
	int	i, id;

	/*
	 * All AX88x90 devices have MII and a PHY, so we use this to weed out
	 * chips that would otherwise make it through the tests we have after
	 * this point.
	 */
	for (i = 0; i < 32; i++) {
		id = ed_miibus_readreg(dev, i, MII_BMSR);
		if (id != 0 && id != 0xffff)
			break;
	}
	/*
	 * Found one, we're good.
	 */
	if (i != 32)
		return (0);
	/*
	 * Didn't find anything, so try to power up and try again.  The PHY
	 * may be not responding because we're in power down mode.
	 */
	if (sc->chip_type == ED_CHIP_TYPE_AX88190)
		return (ENXIO);
	pccard_ccr_write_1(dev, PCCARD_CCR_STATUS, PCCARD_CCR_STATUS_PWRDWN);
	for (i = 0; i < 32; i++) {
		id = ed_miibus_readreg(dev, i, MII_BMSR);
		if (id != 0 && id != 0xffff)
			break;
	}
	/*
	 * Still no joy?  We're AFU, punt.
	 */
	if (i == 32)
		return (ENXIO);
	return (0);
}

/*
 * Special setup for AX88[17]90
 */
static int
ed_pccard_ax88x90(device_t dev, const struct ed_product *pp)
{
	int	error;
	int iobase;
	struct	ed_softc *sc = device_get_softc(dev);

	if (!(pp->flags & NE2000DVF_AX88X90))
		return (ENXIO);

	if (bootverbose)
		device_printf(dev, "Checking AX88x90\n");

	/*
	 * Set the IOBASE Register.  The AX88x90 cards are potentially
	 * multifunction cards, and thus requires a slight workaround.
	 * We write the address the card is at, on the off chance that this
	 * card is not MFC.
	 * XXX I'm not sure that this is still needed...
	 */
	iobase = rman_get_start(sc->port_res);
	pccard_ccr_write_1(dev, PCCARD_CCR_IOBASE0, iobase & 0xff);
	pccard_ccr_write_1(dev, PCCARD_CCR_IOBASE1, (iobase >> 8) & 0xff);

	error = ed_probe_ax88x90_generic(dev, device_get_flags(dev));
	if (error) {
		if (bootverbose)
			device_printf(dev, "probe ax88x90 failed %d\n",
			    error);
		return (error);
	}
	sc->mii_bitbang_ops = &ed_pccard_ax88x90_mii_bitbang_ops;
	error = ed_pccard_ax88x90_check_mii(dev, sc);
	if (error)
		return (error);
	sc->vendor = ED_VENDOR_NOVELL;
	sc->type = ED_TYPE_NE2000;
	if (sc->chip_type == ED_CHIP_TYPE_AX88190)
		sc->type_str = "AX88190";
	else
		sc->type_str = "AX88790";
	return (0);
}

static void
ed_pccard_ax88x90_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct ed_softc *sc;

	sc = device_get_softc(dev);

	ed_asic_outb(sc, ED_AX88X90_MIIBUS, val);
	ed_asic_barrier(sc, ED_AX88X90_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static uint32_t
ed_pccard_ax88x90_mii_bitbang_read(device_t dev)
{
	struct ed_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = ed_asic_inb(sc, ED_AX88X90_MIIBUS);
	ed_asic_barrier(sc, ED_AX88X90_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (val);
}

/*
 * Special setup for TC5299J
 */
static int
ed_pccard_tc5299j(device_t dev, const struct ed_product *pp)
{
	int	error, i, id;
	char *ts;
	struct	ed_softc *sc = device_get_softc(dev);

	if (!(pp->flags & NE2000DVF_TC5299J))
		return (ENXIO);

	if (bootverbose)
		device_printf(dev, "Checking Tc5299j\n");

	error = ed_probe_Novell_generic(dev, device_get_flags(dev));
	if (bootverbose)
		device_printf(dev, "Novell generic probe failed: %d\n", error);
	if (error != 0)
		return (error);

	/*
	 * Check to see if we have a MII PHY ID at any address.  All TC5299J
	 * devices have MII and a PHY, so we use this to weed out chips that
	 * would otherwise make it through the tests we have after this point.
	 */
	sc->mii_bitbang_ops = &ed_pccard_tc5299j_mii_bitbang_ops;
	for (i = 0; i < 32; i++) {
		id = ed_miibus_readreg(dev, i, MII_PHYIDR1);
		if (id != 0 && id != 0xffff)
			break;
	}
	if (i == 32)
		return (ENXIO);
	ts = "TC5299J";
	if (ed_pccard_rom_mac(dev, sc->enaddr) == 0)
		return (ENXIO);
	sc->vendor = ED_VENDOR_NOVELL;
	sc->type = ED_TYPE_NE2000;
	sc->chip_type = ED_CHIP_TYPE_TC5299J;
	sc->type_str = ts;
	return (0);
}

static void
ed_pccard_tc5299j_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct ed_softc *sc;

	sc = device_get_softc(dev);

	/* We are already on page 3. */
	ed_nic_outb(sc, ED_TC5299J_MIIBUS, val);
	ed_nic_barrier(sc, ED_TC5299J_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static uint32_t
ed_pccard_tc5299j_mii_bitbang_read(device_t dev)
{
	struct ed_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	/* We are already on page 3. */
	val = ed_asic_inb(sc, ED_TC5299J_MIIBUS);
	ed_nic_barrier(sc, ED_TC5299J_MIIBUS, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (val);
}

/*
 * MII bus support routines.
 */
static int
ed_miibus_readreg(device_t dev, int phy, int reg)
{
	struct ed_softc *sc;
	int val;
	uint8_t cr = 0;

	sc = device_get_softc(dev);
	/*
	 * The AX88790 has an interesting quirk.  It has an internal phy that
	 * needs a special bit set to access, but can also have additional
	 * external PHYs set for things like HomeNET media.  When accessing
	 * the internal PHY, a bit has to be set, when accessing the external
	 * PHYs, it must be clear.  See Errata 1, page 51, in the AX88790
	 * datasheet for more details.
	 *
	 * Also, PHYs above 16 appear to be phantoms on some cards, but not
	 * others.  Registers read for this are often the same as prior values
	 * read.  Filter all register requests to 17-31.
	 */
	if (sc->chip_type == ED_CHIP_TYPE_AX88790) {
		if (phy > 0x10)
			return (0);
		if (phy == 0x10)
			ed_asic_outb(sc, ED_AX88X90_GPIO,
			    ED_AX88X90_GPIO_INT_PHY);
		else
			ed_asic_outb(sc, ED_AX88X90_GPIO, 0);
		ed_asic_barrier(sc, ED_AX88X90_GPIO, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	} else if (sc->chip_type == ED_CHIP_TYPE_TC5299J) {
		/* Select page 3. */
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		cr = ed_nic_inb(sc, ED_P0_CR);
		ed_nic_outb(sc, ED_P0_CR, cr | ED_CR_PAGE_3);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
	val = mii_bitbang_readreg(dev, sc->mii_bitbang_ops, phy, reg);
	if (sc->chip_type == ED_CHIP_TYPE_TC5299J) {
		/* Restore prior page. */
		ed_nic_outb(sc, ED_P0_CR, cr);
		ed_nic_barrier(sc, ED_P0_CR, 1,
	    	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
	return (val);
}

static int
ed_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct ed_softc *sc;
	uint8_t cr = 0;

	sc = device_get_softc(dev);
	/* See ed_miibus_readreg for details */
	if (sc->chip_type == ED_CHIP_TYPE_AX88790) {
		if (phy > 0x10)
			return (0);
		if (phy == 0x10)
			ed_asic_outb(sc, ED_AX88X90_GPIO,
			    ED_AX88X90_GPIO_INT_PHY);
		else
			ed_asic_outb(sc, ED_AX88X90_GPIO, 0);
		ed_asic_barrier(sc, ED_AX88X90_GPIO, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	} else if (sc->chip_type == ED_CHIP_TYPE_TC5299J) {
		/* Select page 3. */
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		cr = ed_nic_inb(sc, ED_P0_CR);
		ed_nic_outb(sc, ED_P0_CR, cr | ED_CR_PAGE_3);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
	mii_bitbang_writereg(dev, sc->mii_bitbang_ops, phy, reg, data);
	if (sc->chip_type == ED_CHIP_TYPE_TC5299J) {
		/* Restore prior page. */
		ed_nic_outb(sc, ED_P0_CR, cr);
		ed_nic_barrier(sc, ED_P0_CR, 1,
	    	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
	return (0);
}

static int
ed_ifmedia_upd(struct ifnet *ifp)
{
	struct ed_softc *sc;
	int error;

	sc = ifp->if_softc;
	ED_LOCK(sc);
	if (sc->miibus == NULL) {
		ED_UNLOCK(sc);
		return (ENXIO);
	}

	error = ed_pccard_kick_phy(sc);
	ED_UNLOCK(sc);
	return (error);
}

static void
ed_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ed_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	ED_LOCK(sc);
	if (sc->miibus == NULL) {
		ED_UNLOCK(sc);
		return;
	}

	mii = device_get_softc(sc->miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	ED_UNLOCK(sc);
}

static void
ed_child_detached(device_t dev, device_t child)
{
	struct ed_softc *sc;

	sc = device_get_softc(dev);
	if (child == sc->miibus)
		sc->miibus = NULL;
}

static void
ed_pccard_tick(struct ed_softc *sc)
{
	struct mii_data *mii;
	int media = 0;

	ED_ASSERT_LOCKED(sc);
	if (sc->miibus != NULL) {
		mii = device_get_softc(sc->miibus);
		media = mii->mii_media_status;
		mii_tick(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    media != mii->mii_media_status) {
			if (sc->chip_type == ED_CHIP_TYPE_DL10022) {
				ed_asic_outb(sc, ED_DL10022_DIAG,
				    (mii->mii_media_active & IFM_FDX) ?
				    ED_DL10022_COLLISON_DIS : 0);
#ifdef notyet
			} else if (sc->chip_type == ED_CHIP_TYPE_DL10019) {
				write_asic(sc, ED_DL10019_MAGIC,
				    (mii->mii_media_active & IFM_FDX) ?
				    DL19FDUPLX : 0);
#endif
			}
		}
		
	}
}

static device_method_t ed_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ed_pccard_probe),
	DEVMETHOD(device_attach,	ed_pccard_attach),
	DEVMETHOD(device_detach,	ed_detach),

	/* Bus interface */
	DEVMETHOD(bus_child_detached,	ed_child_detached),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	ed_miibus_readreg),
	DEVMETHOD(miibus_writereg,	ed_miibus_writereg),

	DEVMETHOD_END
};

static driver_t ed_pccard_driver = {
	"ed",
	ed_pccard_methods,
	sizeof(struct ed_softc)
};

DRIVER_MODULE(ed, pccard, ed_pccard_driver, ed_devclass, 0, NULL);
DRIVER_MODULE(miibus, ed, miibus_driver, miibus_devclass, 0, NULL);
MODULE_DEPEND(ed, miibus, 1, 1, 1);
MODULE_DEPEND(ed, ether, 1, 1, 1);
PCCARD_PNP_INFO(ed_pccard_products);
