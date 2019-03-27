/*-
 * All Rights Reserved, Copyright (C) Fujitsu Limited 1995
 *
 * This software may be used, modified, copied, distributed, and sold, in
 * both source and binary form provided that the above copyright, these
 * terms and the following disclaimer are retained.  The name of the author
 * and/or the contributor may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND THE CONTRIBUTOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR THE CONTRIBUTOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION.
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
#include <sys/rman.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/fe/mb86960.h>
#include <dev/fe/if_fereg.h>
#include <dev/fe/if_fevar.h>

#include <isa/isavar.h>

/*
 *	ISA specific code.
 */
static int fe_isa_probe(device_t);
static int fe_isa_attach(device_t);

static device_method_t fe_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fe_isa_probe),
	DEVMETHOD(device_attach,	fe_isa_attach),

	{ 0, 0 }
};

static driver_t fe_isa_driver = {
	"fe",
	fe_isa_methods,
	sizeof (struct fe_softc)
};

static int fe_probe_ssi(device_t);
static int fe_probe_jli(device_t);
static int fe_probe_fmv(device_t);
static int fe_probe_lnx(device_t);
static int fe_probe_gwy(device_t);
static int fe_probe_ubn(device_t);

/*
 * Determine if the device is present at a specified I/O address.  The
 * main entry to the driver.
 */
static int
fe_isa_probe(device_t dev)
{
	struct fe_softc *sc;
	int error;

	/* Check isapnp ids */
	if (isa_get_vendorid(dev))
		return (ENXIO);

	/* Prepare for the softc struct.  */
	sc = device_get_softc(dev);
	sc->sc_unit = device_get_unit(dev);

	/* Probe for supported boards.  */
	if ((error = fe_probe_ssi(dev)) == 0)
		goto end;
	fe_release_resource(dev);

	if ((error = fe_probe_jli(dev)) == 0)
		goto end;
	fe_release_resource(dev);

	if ((error = fe_probe_fmv(dev)) == 0)
		goto end;
	fe_release_resource(dev);

	if ((error = fe_probe_lnx(dev)) == 0)
		goto end;
	fe_release_resource(dev);

	if ((error = fe_probe_ubn(dev)) == 0)
		goto end;
	fe_release_resource(dev);

	if ((error = fe_probe_gwy(dev)) == 0)
		goto end;
	fe_release_resource(dev);

end:
	if (error == 0)
		error = fe_alloc_irq(dev, 0);

	fe_release_resource(dev);
	return (error);
}

static int
fe_isa_attach(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	int error = 0;

	/*
	 * Note: these routines aren't expected to fail since we also call
	 * them in the probe routine.  But coverity complains, so we'll honor
	 * that complaint since the intention here was never to ignore them..
	 */
	if (sc->port_used) {
		error = fe_alloc_port(dev, sc->port_used);
		if (error != 0)
			return (error);
	}
	error = fe_alloc_irq(dev, 0);
	if (error != 0)
		return (error);

	return fe_attach(dev);
}


/*
 * Probe and initialization for Fujitsu FMV-180 series boards
 */

static void
fe_init_fmv(struct fe_softc *sc)
{
	/* Initialize ASIC.  */
	fe_outb(sc, FE_FMV3, 0);
	fe_outb(sc, FE_FMV10, 0);

#if 0
	/* "Refresh" hardware configuration.  FIXME.  */
	fe_outb(sc, FE_FMV2, fe_inb(sc, FE_FMV2));
#endif

	/* Turn the "master interrupt control" flag of ASIC on.  */
	fe_outb(sc, FE_FMV3, FE_FMV3_IRQENB);
}

static void
fe_msel_fmv184(struct fe_softc *sc)
{
	u_char port;

	/* FMV-184 has a special "register" to switch between AUI/BNC.
	   Determine the value to write into the register, based on the
	   user-specified media selection.  */
	port = (IFM_SUBTYPE(sc->media.ifm_media) == IFM_10_2) ? 0x00 : 0x01;

	/* The register is #5 on exntesion register bank...
	   (Details of the register layout is not yet discovered.)  */
	fe_outb(sc, 0x1B, 0x46);	/* ??? */
	fe_outb(sc, 0x1E, 0x04);	/* select ex-reg #4.  */
	fe_outb(sc, 0x1F, 0xC8);	/* ??? */
	fe_outb(sc, 0x1E, 0x05);	/* select ex-reg #5.  */
	fe_outb(sc, 0x1F, port);	/* Switch the media.  */
	fe_outb(sc, 0x1E, 0x04);	/* select ex-reg #4.  */
	fe_outb(sc, 0x1F, 0x00);	/* ??? */
	fe_outb(sc, 0x1B, 0x00);	/* ??? */

	/* Make sure to select "external tranceiver" on MB86964.  */
	fe_outb(sc, FE_BMPR13, sc->proto_bmpr13 | FE_B13_PORT_AUI);
}

static int
fe_probe_fmv(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	int n;
	rman_res_t iobase, irq;

	static u_short const irqmap [ 4 ] = { 3, 7, 10, 15 };

	static struct fe_simple_probe_struct const probe_table [] = {
		{ FE_DLCR2, 0x71, 0x00 },
		{ FE_DLCR4, 0x08, 0x00 },

		{ FE_FMV0, 0x78, 0x50 },	/* ERRDY+PRRDY */
		{ FE_FMV1, 0xB0, 0x00 },	/* FMV-183/4 has 0x48 bits. */
		{ FE_FMV3, 0x7F, 0x00 },

		{ 0 }
	};

	/* Board subtypes; it lists known FMV-180 variants.  */
	struct subtype {
		u_short mcode;
		u_short mbitmap;
		u_short defmedia;
		char const * str;
	};
	static struct subtype const typelist [] = {
	    { 0x0005, MB_HA|MB_HT|MB_H5, MB_HA, "FMV-181"		},
	    { 0x0105, MB_HA|MB_HT|MB_H5, MB_HA, "FMV-181A"		},
	    { 0x0003, MB_HM,             MB_HM, "FMV-182"		},
	    { 0x0103, MB_HM,             MB_HM, "FMV-182A"		},
	    { 0x0804, MB_HT,             MB_HT, "FMV-183"		},
	    { 0x0C04, MB_HT,             MB_HT, "FMV-183 (on-board)"	},
	    { 0x0803, MB_H2|MB_H5,       MB_H2, "FMV-184"		},
	    { 0,      MB_HA,             MB_HA, "unknown FMV-180 (?)"	},
	};
	struct subtype const * type;

	/* Media indicator and "Hardware revision ID"  */
	u_short mcode;

	/* See if the specified address is possible for FMV-180
           series.  220, 240, 260, 280, 2A0, 2C0, 300, and 340 are
           allowed for all boards, and 200, 2E0, 320, 360, 380, 3A0,
           3C0, and 3E0 for PnP boards.  */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	if ((iobase & ~0x1E0) != 0x200)
		return ENXIO;

	/* FMV-180 occupies 32 I/O addresses. */
	if (fe_alloc_port(dev, 32))
		return ENXIO;

	/* Setup an I/O address mapping table and some others.  */
	fe_softc_defaults(sc);

	/* Simple probe.  */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* Get our station address from EEPROM, and make sure it is
           Fujitsu's.  */
	fe_inblk(sc, FE_FMV4, sc->enaddr, ETHER_ADDR_LEN);
	if (!fe_valid_Ether_p(sc->enaddr, 0x00000E))
		return ENXIO;

	/* Find the supported media and "hardware revision" to know
           the model identification.  */
	mcode = (fe_inb(sc, FE_FMV0) & FE_FMV0_MEDIA)
	     | ((fe_inb(sc, FE_FMV1) & FE_FMV1_REV) << 8);

	/* Determine the card type.  */
	for (type = typelist; type->mcode != 0; type++) {
		if (type->mcode == mcode)
			break;
	}
	if (type->mcode == 0) {
	  	/* Unknown card type...  Hope the driver works.  */
		sc->stability |= UNSTABLE_TYPE;
		if (bootverbose) {
			device_printf(dev, "unknown config: %x-%x-%x-%x\n",
				      fe_inb(sc, FE_FMV0),
				      fe_inb(sc, FE_FMV1),
				      fe_inb(sc, FE_FMV2),
				      fe_inb(sc, FE_FMV3));
		}
	}

	/* Setup the board type and media information.  */
	sc->type = FE_TYPE_FMV;
	sc->typestr = type->str;
	sc->mbitmap = type->mbitmap;
	sc->defmedia = type->defmedia;
	sc->msel = fe_msel_965;

	if (type->mbitmap == (MB_H2 | MB_H5)) {
		/* FMV184 requires a special media selection procedure.  */
		sc->msel = fe_msel_fmv184;
	}

	/*
	 * An FMV-180 has been probed.
	 * Determine which IRQ to be used.
	 *
	 * In this version, we give a priority to the kernel config file.
	 * If the EEPROM and config don't match, say it to the user for
	 * an attention.
	 */
	n = (fe_inb(sc, FE_FMV2) & FE_FMV2_IRS)	>> FE_FMV2_IRS_SHIFT;

	irq = 0;
	bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL);
	if (irq == NO_IRQ) {
		/* Just use the probed value.  */
		bus_set_resource(dev, SYS_RES_IRQ, 0, irqmap[n], 1);
	} else if (irq != irqmap[n]) {
		/* Don't match.  */
		sc->stability |= UNSTABLE_IRQ;
	}

	/* We need an init hook to initialize ASIC before we start.  */
	sc->init = fe_init_fmv;

	return 0;
}

/*
 * Fujitsu MB86965 JLI mode probe routines.
 *
 * 86965 has a special operating mode called JLI (mode 0), under which
 * the chip interfaces with ISA bus with a software-programmable
 * configuration.  (The Fujitsu document calls the feature "Plug and
 * play," but it is not compatible with the ISA-PnP spec. designed by
 * Intel and Microsoft.)  Ethernet cards designed to use JLI are
 * almost same, but there are two things which require board-specific
 * probe routines: EEPROM layout and IRQ pin connection.
 *
 * JLI provides a handy way to access EEPROM which should contains the
 * chip configuration information (such as I/O port address) as well
 * as Ethernet station (MAC) address.  The chip configuration info. is
 * stored on a fixed location.  However, the station address can be
 * located anywhere in the EEPROM; it is up to the board designer to
 * determine the location.  (The manual just says "somewhere in the
 * EEPROM.")  The fe driver must somehow find out the correct
 * location.
 *
 * Another problem resides in the IRQ pin connection.  JLI provides a
 * user to choose an IRQ from up to four predefined IRQs.  The 86965
 * chip has a register to select one out of the four possibilities.
 * However, the selection is against the four IRQ pins on the chip.
 * (So-called IRQ-A, -B, -C and -D.)  It is (again) up to the board
 * designer to determine which pin to connect which IRQ line on the
 * ISA bus.  We need a vendor (or model, for some vendor) specific IRQ
 * mapping table.
 * 
 * The routine fe_probe_jli() provides all probe and initialization
 * processes which are common to all JLI implementation, and sub-probe
 * routines supply board-specific actions.
 *
 * JLI sub-probe routine has the following template:
 *
 *	u_short const * func (struct fe_softc * sc, u_char const * eeprom);
 *
 * where eeprom is a pointer to an array of 32 byte data read from the
 * config EEPROM on the board.  It returns an IRQ mapping table for the
 * board, when the corresponding implementation is detected.  It
 * returns a NULL otherwise.
 * 
 * Primary purpose of the functin is to analize the config EEPROM,
 * determine if it matches with the pattern of that of supported card,
 * and extract necessary information from it.  One of the information
 * expected to be extracted from EEPROM is the Ethernet station (MAC)
 * address, which must be set to the softc table of the interface by
 * the board-specific routine.
 */

/* JLI sub-probe for Allied-Telesyn/Allied-Telesis AT1700/RE2000 series.  */
static u_short const *
fe_probe_jli_ati(struct fe_softc * sc, u_char const * eeprom)
{
	int i;
	static u_short const irqmaps_ati [4][4] =
	{
		{  3,  4,  5,  9 },
		{ 10, 11, 12, 15 },
		{  3, 11,  5, 15 },
		{ 10, 11, 14, 15 },
	};

	/* Make sure the EEPROM contains Allied-Telesis/Allied-Telesyn
	   bit pattern.  */
	if (eeprom[1] != 0x00) return NULL;
	for (i =  2; i <  8; i++) if (eeprom[i] != 0xFF) return NULL;
	for (i = 14; i < 24; i++) if (eeprom[i] != 0xFF) return NULL;

	/* Get our station address from EEPROM, and make sure the
           EEPROM contains ATI's address.  */
	bcopy(eeprom + 8, sc->enaddr, ETHER_ADDR_LEN);
	if (!fe_valid_Ether_p(sc->enaddr, 0x0000F4))
		return NULL;

	/*
	 * The following model identification codes are stolen
	 * from the NetBSD port of the fe driver.  My reviewers
	 * suggested minor revision.
	 */

	/* Determine the card type.  */
	switch (eeprom[FE_ATI_EEP_MODEL]) {
	  case FE_ATI_MODEL_AT1700T:
		sc->typestr = "AT-1700T/RE2001";
		sc->mbitmap = MB_HT;
		sc->defmedia = MB_HT;
		break;
	  case FE_ATI_MODEL_AT1700BT:
		sc->typestr = "AT-1700BT/RE2003";
		sc->mbitmap = MB_HA | MB_HT | MB_H2;
		break;
	  case FE_ATI_MODEL_AT1700FT:
		sc->typestr = "AT-1700FT/RE2009";
		sc->mbitmap = MB_HA | MB_HT | MB_HF;
		break;
	  case FE_ATI_MODEL_AT1700AT:
		sc->typestr = "AT-1700AT/RE2005";
		sc->mbitmap = MB_HA | MB_HT | MB_H5;
		break;
	  default:
		sc->typestr = "unknown AT-1700/RE2000";
		sc->stability |= UNSTABLE_TYPE | UNSTABLE_IRQ;
		break;
	}
	sc->type = FE_TYPE_JLI;

#if 0
	/* Should we extract default media from eeprom?  Linux driver
	   for AT1700 does it, although previous releases of FreeBSD
	   don't.  FIXME.  */
	/* Determine the default media selection from the config
           EEPROM.  The byte at offset EEP_MEDIA is believed to
           contain BMPR13 value to be set.  We just ignore STP bit or
           squelch bit, since we don't support those.  (It is
           intentional.)  */
	switch (eeprom[FE_ATI_EEP_MEDIA] & FE_B13_PORT) {
	    case FE_B13_AUTO:
		sc->defmedia = MB_HA;
		break;
	    case FE_B13_TP:
		sc->defmedia = MB_HT;
		break;
	    case FE_B13_AUI:
		sc->defmedia = sc->mbitmap & (MB_H2|MB_H5|MB_H5); /*XXX*/
		break;
	    default:	    
		sc->defmedia = MB_HA;
		break;
	}

	/* Make sure the default media is compatible with the supported
	   ones.  */
	if ((sc->defmedia & sc->mbitmap) == 0) {
		if (sc->defmedia == MB_HA) {
			sc->defmedia = MB_HT;
		} else {
			sc->defmedia = MB_HA;
		}
	}
#endif	

	/*
	 * Try to determine IRQ settings.
	 * Different models use different ranges of IRQs.
	 */
	switch ((eeprom[FE_ATI_EEP_REVISION] & 0xf0)
	       |(eeprom[FE_ATI_EEP_MAGIC]    & 0x04)) {
	    case 0x30: case 0x34: return irqmaps_ati[3];
	    case 0x10: case 0x14:
	    case 0x50: case 0x54: return irqmaps_ati[2];
	    case 0x44: case 0x64: return irqmaps_ati[1];
	    default:		  return irqmaps_ati[0];
	}
}

/* JLI sub-probe and msel hook for ICL Ethernet.  */
static void
fe_msel_icl(struct fe_softc *sc)
{
	u_char d4;

	/* Switch between UTP and "external tranceiver" as always.  */    
	fe_msel_965(sc);

	/* The board needs one more bit (on DLCR4) be set appropriately.  */
	if (IFM_SUBTYPE(sc->media.ifm_media) == IFM_10_5) {
		d4 = sc->proto_dlcr4 | FE_D4_CNTRL;
	} else {
		d4 = sc->proto_dlcr4 & ~FE_D4_CNTRL;
	}
	fe_outb(sc, FE_DLCR4, d4);
}

static u_short const *
fe_probe_jli_icl(struct fe_softc * sc, u_char const * eeprom)
{
	int i;
	u_short defmedia;
	u_char d6;
	static u_short const irqmap_icl [4] = { 9, 10, 5, 15 };

	/* Make sure the EEPROM contains ICL bit pattern.  */
	for (i = 24; i < 39; i++) {
	    if (eeprom[i] != 0x20 && (eeprom[i] & 0xF0) != 0x30) return NULL;
	}
	for (i = 112; i < 122; i++) {
	    if (eeprom[i] != 0x20 && (eeprom[i] & 0xF0) != 0x30) return NULL;
	}

	/* Make sure the EEPROM contains ICL's permanent station
           address.  If it isn't, probably this board is not an
           ICL's.  */
	if (!fe_valid_Ether_p(eeprom+122, 0x00004B))
		return NULL;

	/* Check if the "configured" Ethernet address in the EEPROM is
	   valid.  Use it if it is, or use the "permanent" address instead.  */
	if (fe_valid_Ether_p(eeprom+4, 0x020000)) {
		/* The configured address is valid.  Use it.  */
		bcopy(eeprom+4, sc->enaddr, ETHER_ADDR_LEN);
	} else {
		/* The configured address is invalid.  Use permanent.  */
		bcopy(eeprom+122, sc->enaddr, ETHER_ADDR_LEN);
	}

	/* Determine model and supported media.  */
	switch (eeprom[0x5E]) {
	    case 0:
	        sc->typestr = "EtherTeam16i/COMBO";
	        sc->mbitmap = MB_HA | MB_HT | MB_H5 | MB_H2;
		break;
	    case 1:
		sc->typestr = "EtherTeam16i/TP";
	        sc->mbitmap = MB_HT;
		break;
	    case 2:
		sc->typestr = "EtherTeam16i/ErgoPro";
		sc->mbitmap = MB_HA | MB_HT | MB_H5;
		break;
	    case 4:
		sc->typestr = "EtherTeam16i/DUO";
		sc->mbitmap = MB_HA | MB_HT | MB_H2;
		break;
	    default:
		sc->typestr = "EtherTeam16i";
		sc->stability |= UNSTABLE_TYPE;
		if (bootverbose) {
		    printf("fe%d: unknown model code %02x for EtherTeam16i\n",
			   sc->sc_unit, eeprom[0x5E]);
		}
		break;
	}
	sc->type = FE_TYPE_JLI;

	/* I'm not sure the following msel hook is required by all
           models or COMBO only...  FIXME.  */
	sc->msel = fe_msel_icl;

	/* Make the configured media selection the default media.  */
	switch (eeprom[0x28]) {
	    case 0: defmedia = MB_HA; break;
	    case 1: defmedia = MB_H5; break;
	    case 2: defmedia = MB_HT; break;
	    case 3: defmedia = MB_H2; break;
	    default: 
		if (bootverbose) {
			printf("fe%d: unknown default media: %02x\n",
			       sc->sc_unit, eeprom[0x28]);
		}
		defmedia = MB_HA;
		break;
	}

	/* Make sure the default media is compatible with the
	   supported media.  */
	if ((defmedia & sc->mbitmap) == 0) {
		if (bootverbose) {
			printf("fe%d: default media adjusted\n", sc->sc_unit);
		}
		defmedia = sc->mbitmap;
	}

	/* Keep the determined default media.  */
	sc->defmedia = defmedia;

	/* ICL has "fat" models.  We have to program 86965 to properly
	   reflect the hardware.  */
	d6 = sc->proto_dlcr6 & ~(FE_D6_BUFSIZ | FE_D6_BBW);
	switch ((eeprom[0x61] << 8) | eeprom[0x60]) {
	    case 0x2008: d6 |= FE_D6_BUFSIZ_32KB | FE_D6_BBW_BYTE; break;
	    case 0x4010: d6 |= FE_D6_BUFSIZ_64KB | FE_D6_BBW_WORD; break;
	    default:
		/* We can't support it, since we don't know which bits
		   to set in DLCR6.  */
		printf("fe%d: unknown SRAM config for ICL\n", sc->sc_unit);
		return NULL;
	}
	sc->proto_dlcr6 = d6;

	/* Returns the IRQ table for the ICL board.  */
	return irqmap_icl;
}

/* JLI sub-probe for RATOC REX-5586/5587.  */
static u_short const *
fe_probe_jli_rex(struct fe_softc * sc, u_char const * eeprom)
{
	int i;
	static u_short const irqmap_rex [4] = { 3, 4, 5, NO_IRQ };

	/* Make sure the EEPROM contains RATOC's config pattern.  */
	if (eeprom[1] != eeprom[0]) return NULL;
	for (i = 8; i < 32; i++) if (eeprom[i] != 0xFF) return NULL;

	/* Get our station address from EEPROM.  Note that RATOC
	   stores it "byte-swapped" in each word.  (I don't know why.)
	   So, we just can't use bcopy().*/
	sc->enaddr[0] = eeprom[3];
	sc->enaddr[1] = eeprom[2];
	sc->enaddr[2] = eeprom[5];
	sc->enaddr[3] = eeprom[4];
	sc->enaddr[4] = eeprom[7];
	sc->enaddr[5] = eeprom[6];

	/* Make sure the EEPROM contains RATOC's station address.  */
	if (!fe_valid_Ether_p(sc->enaddr, 0x00C0D0))
		return NULL;

	/* I don't know any sub-model identification.  */
	sc->type = FE_TYPE_JLI;
	sc->typestr = "REX-5586/5587";

	/* Returns the IRQ for the RATOC board.  */
	return irqmap_rex;
}

/* JLI sub-probe for Unknown board.  */
static u_short const *
fe_probe_jli_unk(struct fe_softc * sc, u_char const * eeprom)
{
	int i, n, romsize;
	static u_short const irqmap [4] = { NO_IRQ, NO_IRQ, NO_IRQ, NO_IRQ };

	/* The generic JLI probe considered this board has an 86965
	   in JLI mode, but any other board-specific routines could
	   not find the matching implementation.  So, we "guess" the
	   location by looking for a bit pattern which looks like a
	   MAC address.  */

	/* Determine how large the EEPROM is.  */
	for (romsize = JLI_EEPROM_SIZE/2; romsize > 16; romsize >>= 1) {
		for (i = 0; i < romsize; i++) {
			if (eeprom[i] != eeprom[i+romsize])
				break;
		}
		if (i < romsize)
			break;
	}
	romsize <<= 1;

	/* Look for a bit pattern which looks like a MAC address.  */
	for (n = 2; n <= romsize - ETHER_ADDR_LEN; n += 2) {
		if (!fe_valid_Ether_p(eeprom + n, 0x000000))
			continue;
	}

	/* If no reasonable address was found, we can't go further.  */
	if (n > romsize - ETHER_ADDR_LEN)
		return NULL;

	/* Extract our (guessed) station address.  */
	bcopy(eeprom+n, sc->enaddr, ETHER_ADDR_LEN);

	/* We are not sure what type of board it is... */
	sc->type = FE_TYPE_JLI;
	sc->typestr = "(unknown JLI)";
	sc->stability |= UNSTABLE_TYPE | UNSTABLE_MAC;

	/* Returns the totally unknown IRQ mapping table.  */
	return irqmap;
}

/*
 * Probe and initialization for all JLI implementations.
 */

static int
fe_probe_jli(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	int i, n, error, xirq;
	rman_res_t iobase, irq;
	u_char eeprom [JLI_EEPROM_SIZE];
	u_short const * irqmap;

	static u_short const baseaddr [8] =
		{ 0x260, 0x280, 0x2A0, 0x240, 0x340, 0x320, 0x380, 0x300 };
	static struct fe_simple_probe_struct const probe_table [] = {
		{ FE_DLCR1,  0x20, 0x00 },
		{ FE_DLCR2,  0x50, 0x00 },
		{ FE_DLCR4,  0x08, 0x00 },
		{ FE_DLCR5,  0x80, 0x00 },
#if 0
		{ FE_BMPR16, 0x1B, 0x00 },
		{ FE_BMPR17, 0x7F, 0x00 },
#endif
		{ 0 }
	};

	/*
	 * See if the specified address is possible for MB86965A JLI mode.
	 */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	for (i = 0; i < 8; i++) {
		if (baseaddr[i] == iobase)
			break;
	}
	if (i == 8)
		return ENXIO;

	/* 86965 JLI occupies 32 I/O addresses. */
	if (fe_alloc_port(dev, 32))
		return ENXIO;

	/* Fill the softc struct with reasonable default.  */
	fe_softc_defaults(sc);

	/*
	 * We should test if MB86965A is on the base address now.
	 * Unfortunately, it is very hard to probe it reliably, since
	 * we have no way to reset the chip under software control.
	 * On cold boot, we could check the "signature" bit patterns
	 * described in the Fujitsu document.  On warm boot, however,
	 * we can predict almost nothing about register values.
	 */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* Check if our I/O address matches config info on 86965.  */
	n = (fe_inb(sc, FE_BMPR19) & FE_B19_ADDR) >> FE_B19_ADDR_SHIFT;
	if (baseaddr[n] != iobase)
		return ENXIO;

	/*
	 * We are now almost sure we have an MB86965 at the given
	 * address.  So, read EEPROM through it.  We have to write
	 * into LSI registers to read from EEPROM.  I want to avoid it
	 * at this stage, but I cannot test the presence of the chip
	 * any further without reading EEPROM.  FIXME.
	 */
	fe_read_eeprom_jli(sc, eeprom);

	/* Make sure that config info in EEPROM and 86965 agree.  */
	if (eeprom[FE_EEPROM_CONF] != fe_inb(sc, FE_BMPR19))
		return ENXIO;

	/* Use 86965 media selection scheme, unless othewise
           specified.  It is "AUTO always" and "select with BMPR13."
           This behaviour covers most of the 86965 based board (as
           minimum requirements.)  It is backward compatible with
           previous versions, also.  */
	sc->mbitmap = MB_HA;
	sc->defmedia = MB_HA;
	sc->msel = fe_msel_965;

	/* Perform board-specific probe, one by one.  Note that the
           order of probe is important and should not be changed
           arbitrarily.  */
	if ((irqmap = fe_probe_jli_ati(sc, eeprom)) == NULL
	 && (irqmap = fe_probe_jli_rex(sc, eeprom)) == NULL
	 && (irqmap = fe_probe_jli_icl(sc, eeprom)) == NULL
	 && (irqmap = fe_probe_jli_unk(sc, eeprom)) == NULL)
		return ENXIO;

	/* Find the IRQ read from EEPROM.  */
	n = (fe_inb(sc, FE_BMPR19) & FE_B19_IRQ) >> FE_B19_IRQ_SHIFT;
	xirq = irqmap[n];

	/* Try to determine IRQ setting.  */
	error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL);
	if (error && xirq == NO_IRQ) {
		/* The device must be configured with an explicit IRQ.  */
		device_printf(dev, "IRQ auto-detection does not work\n");
		return ENXIO;
	} else if (error && xirq != NO_IRQ) {
		/* Just use the probed IRQ value.  */
		bus_set_resource(dev, SYS_RES_IRQ, 0, xirq, 1);
	} else if (!error && xirq == NO_IRQ) {
		/* No problem.  Go ahead.  */
	} else if (irq == xirq) {
		/* Good.  Go ahead.  */
	} else {
		/* User must be warned in this case.  */
		sc->stability |= UNSTABLE_IRQ;
	}

	/* Setup a hook, which resets te 86965 when the driver is being
           initialized.  This may solve a nasty bug.  FIXME.  */
	sc->init = fe_init_jli;

	return 0;
}

/* Probe for TDK LAK-AX031, which is an SSi 78Q8377A based board.  */
static int
fe_probe_ssi(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	rman_res_t iobase, irq;

	u_char eeprom [SSI_EEPROM_SIZE];
	static struct fe_simple_probe_struct probe_table [] = {
		{ FE_DLCR2, 0x08, 0x00 },
		{ FE_DLCR4, 0x08, 0x00 },
		{ 0 }
	};

	/* See if the specified I/O address is possible for 78Q8377A.  */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	if ((iobase & ~0x3F0) != 0x000)
                return ENXIO;

	/* We have 16 registers.  */
	if (fe_alloc_port(dev, 16))
		return ENXIO;

	/* Fill the softc struct with default values.  */
	fe_softc_defaults(sc);

	/* See if the card is on its address.  */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* We now have to read the config EEPROM.  We should be very
           careful, since doing so destroys a register.  (Remember, we
           are not yet sure we have a LAK-AX031 board here.)  Don't
           remember to select BMPRs bofore reading EEPROM, since other
           register bank may be selected before the probe() is called.  */
	fe_read_eeprom_ssi(sc, eeprom);

	/* Make sure the Ethernet (MAC) station address is of TDK's.  */
	if (!fe_valid_Ether_p(eeprom+FE_SSI_EEP_ADDR, 0x008098))
		return ENXIO;
	bcopy(eeprom + FE_SSI_EEP_ADDR, sc->enaddr, ETHER_ADDR_LEN);

	/* This looks like a TDK-AX031 board.  It requires an explicit
	   IRQ setting in config, since we currently don't know how we
	   can find the IRQ value assigned by ISA PnP manager.  */
	if (bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL) != 0) {
		fe_irq_failure("LAK-AX031", sc->sc_unit, NO_IRQ, NULL);
		return ENXIO;
	}

	/* Fill softc struct accordingly.  */
	sc->type = FE_TYPE_SSI;
	sc->typestr = "LAK-AX031";
	sc->mbitmap = MB_HT;
	sc->defmedia = MB_HT;

	return 0;
}

/*
 * Probe and initialization for TDK/LANX LAC-AX012/013 boards.
 */
static int
fe_probe_lnx(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	rman_res_t iobase, irq;

	u_char eeprom [LNX_EEPROM_SIZE];
	static struct fe_simple_probe_struct probe_table [] = {
		{ FE_DLCR2, 0x58, 0x00 },
		{ FE_DLCR4, 0x08, 0x00 },
		{ 0 }
	};

	/* See if the specified I/O address is possible for TDK/LANX boards. */
	/* 300, 320, 340, and 360 are allowed.  */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	if ((iobase & ~0x060) != 0x300)
                return ENXIO;

	/* We have 32 registers.  */
	if (fe_alloc_port(dev, 32))
		return ENXIO;

	/* Fill the softc struct with default values.  */
	fe_softc_defaults(sc);

	/* See if the card is on its address.  */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* We now have to read the config EEPROM.  We should be very
           careful, since doing so destroys a register.  (Remember, we
           are not yet sure we have a LAC-AX012/AX013 board here.)  */
	fe_read_eeprom_lnx(sc, eeprom);

	/* Make sure the Ethernet (MAC) station address is of TDK/LANX's.  */
	if (!fe_valid_Ether_p(eeprom, 0x008098))
		return ENXIO;
	bcopy(eeprom, sc->enaddr, ETHER_ADDR_LEN);

	/* This looks like a TDK/LANX board.  It requires an
	   explicit IRQ setting in config.  Make sure we have one,
	   determining an appropriate value for the IRQ control
	   register.  */
	irq = 0;
	bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL);
	switch (irq) {
	case 3: sc->priv_info = 0x40 | LNX_CLK_LO | LNX_SDA_HI; break;
	case 4: sc->priv_info = 0x20 | LNX_CLK_LO | LNX_SDA_HI; break;
	case 5: sc->priv_info = 0x10 | LNX_CLK_LO | LNX_SDA_HI; break;
	case 9: sc->priv_info = 0x80 | LNX_CLK_LO | LNX_SDA_HI; break;
	default:
		fe_irq_failure("LAC-AX012/AX013", sc->sc_unit, irq, "3/4/5/9");
		return ENXIO;
	}

	/* Fill softc struct accordingly.  */
	sc->type = FE_TYPE_LNX;
	sc->typestr = "LAC-AX012/AX013";
	sc->init = fe_init_lnx;

	return 0;
}

/*
 * Probe and initialization for Gateway Communications' old cards.
 */
static int
fe_probe_gwy(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	rman_res_t iobase, irq;

	static struct fe_simple_probe_struct probe_table [] = {
	    /*	{ FE_DLCR2, 0x70, 0x00 }, */
		{ FE_DLCR2, 0x58, 0x00 },
		{ FE_DLCR4, 0x08, 0x00 },
		{ 0 }
	};

	/* See if the specified I/O address is possible for Gateway boards.  */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	if ((iobase & ~0x1E0) != 0x200)
		return ENXIO;

	/* That's all.  The card occupies 32 I/O addresses, as always.  */
	if (fe_alloc_port(dev, 32))
		return ENXIO;

	/* Setup an I/O address mapping table and some others.  */
	fe_softc_defaults(sc);

	/* See if the card is on its address.  */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* Get our station address from EEPROM. */
	fe_inblk(sc, 0x18, sc->enaddr, ETHER_ADDR_LEN);

	/* Make sure it is Gateway Communication's.  */
	if (!fe_valid_Ether_p(sc->enaddr, 0x000061))
		return ENXIO;

	/* Gateway's board requires an explicit IRQ to work, since it
	   is not possible to probe the setting of jumpers.  */
	if (bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL) != 0) {
		fe_irq_failure("Gateway Ethernet", sc->sc_unit, NO_IRQ, NULL);
		return ENXIO;
	}

	/* Fill softc struct accordingly.  */
	sc->type = FE_TYPE_GWY;
	sc->typestr = "Gateway Ethernet (Fujitsu chipset)";

	return 0;
}

/* Probe and initialization for Ungermann-Bass Network
   K.K. "Access/PC" boards.  */
static int
fe_probe_ubn(device_t dev)
{
	struct fe_softc *sc = device_get_softc(dev);
	rman_res_t iobase, irq;
#if 0
	u_char sum;
#endif
	static struct fe_simple_probe_struct const probe_table [] = {
		{ FE_DLCR2, 0x58, 0x00 },
		{ FE_DLCR4, 0x08, 0x00 },
		{ 0 }
	};

	/* See if the specified I/O address is possible for AccessPC/ISA.  */
	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, &iobase, NULL) != 0)
		return ENXIO;
	if ((iobase & ~0x0E0) != 0x300)
		return ENXIO;

	/* We have 32 registers.  */
	if (fe_alloc_port(dev, 32))
		return ENXIO;

	/* Setup an I/O address mapping table and some others.  */
	fe_softc_defaults(sc);

	/* Simple probe.  */
	if (!fe_simple_probe(sc, probe_table))
		return ENXIO;

	/* Get our station address form ID ROM and make sure it is UBN's.  */
	fe_inblk(sc, 0x18, sc->enaddr, ETHER_ADDR_LEN);
	if (!fe_valid_Ether_p(sc->enaddr, 0x00DD01))
		return ENXIO;
#if 0
	/* Calculate checksum.  */
	sum = fe_inb(sc, 0x1e);
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		sum ^= sc->enaddr[i];
	}
	if (sum != 0)
		return ENXIO;
#endif
	/* This looks like an AccessPC/ISA board.  It requires an
	   explicit IRQ setting in config.  Make sure we have one,
	   determining an appropriate value for the IRQ control
	   register.  */
	irq = 0;
	bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, NULL);
	switch (irq) {
	case 3:  sc->priv_info = 0x02; break;
	case 4:  sc->priv_info = 0x04; break;
	case 5:  sc->priv_info = 0x08; break;
	case 10: sc->priv_info = 0x10; break;
	default:
		fe_irq_failure("Access/PC", sc->sc_unit, irq, "3/4/5/10");
		return ENXIO;
	}

	/* Fill softc struct accordingly.  */
	sc->type = FE_TYPE_UBN;
	sc->typestr = "Access/PC";
	sc->init = fe_init_ubn;

	return 0;
}

DRIVER_MODULE(fe, isa, fe_isa_driver, fe_devclass, 0, 0);
